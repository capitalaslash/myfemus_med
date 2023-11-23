#include "fem_ttu.hpp"
#include "FemusMedCoupling.hpp"

#include <FemusInit.hpp>
#include <NonLinearImplicitSystem.hpp>

namespace femus {

// // =======================================================
//   MyFEMuSMED::MyFEMuSMED(int argc, char *args[], const std::string &mesh_file, const unsigned &numberOfUniformLevels)
//     : _mpinit(argc, args, MPI_COMM_WORLD),
//       _numberOfUniformLevels(numberOfUniformLevels),
//       _mlMsh(_numberOfUniformLevels, _numberOfUniformLevels, mesh_file.c_str(), "seventh", 1.0, nullptr),
//       _mlSol(&_mlMsh),
//       _mlProb(&_mlSol),
//       _vtkIO(&_mlSol) {
//
//     _mlMsh.PrintInfo();
//     unsigned const dim = _mlMsh.GetDimension();
//
//     _mlSol.AddSolution("U", LAGRANGE, SECOND, 2);
//     _mlSol.AddSolution("V", LAGRANGE, FIRST, 2);
//     _mlSol.AddSolution("P", DISCONTINUOUS_POLYNOMIAL, FIRST, 2);
//
//     _mlSol.Initialize("U", SetInitialCondition, &_mlProb);
//     _mlSol.Initialize("V", SetInitialCondition, &_mlProb);
//     _mlSol.Initialize("P", SetInitialCondition, &_mlProb);
//
//     // attach the boundary condition function and generate boundary data
//     _mlSol.AttachSetBoundaryConditionFunction(SetBoundaryCondition);
//     _mlSol.GenerateBdc("All");
//
//     std::vector<std::string> variablesToBePrinted = {"All"};
//
//     // Print
//     _vtkIO.SetDebugOutput(true);
//     _vtkIO.Write("RESU", "biquadratic", variablesToBePrinted, 0);
//   }

  static const std::vector<std::vector<INTERP_KERNEL::NormalizedCellType>> myFemusToMEDMapping = {
    {INTERP_KERNEL::NORM_HEXA8, INTERP_KERNEL::NORM_HEXA20, INTERP_KERNEL::NORM_HEXA27},
    {INTERP_KERNEL::NORM_TETRA4, INTERP_KERNEL::NORM_TETRA10, INTERP_KERNEL::NORM_TETRA10},
    {INTERP_KERNEL::NORM_PENTA6, INTERP_KERNEL::NORM_PENTA15, INTERP_KERNEL::NORM_PENTA15},
    {INTERP_KERNEL::NORM_QUAD4, INTERP_KERNEL::NORM_QUAD8, INTERP_KERNEL::NORM_QUAD9},
    {INTERP_KERNEL::NORM_TRI3, INTERP_KERNEL::NORM_TRI6, INTERP_KERNEL::NORM_TRI7},
    {INTERP_KERNEL::NORM_SEG2, INTERP_KERNEL::NORM_SEG3, INTERP_KERNEL::NORM_SEG3},
  };

// =======================================================
  void FemusMedCoupling::Femus2MedMesh(const unsigned &mshType) {

    if (mshType > 2) {
      std::cout << "Error in void MyFEMuSMED::femus_to_med_mesh (const unsigned &mshType)!!!\n";
      std::cout << "Only linear \"0\", quadratic \"1\", or biquadratic \"2\" meshTypes are available\n";
      abort();
    }

    //Solution* sol = _mlSol.GetSolutionLevel(_numberOfUniformLevels - 1);
    //Mesh* msh = sol->GetMesh();    // pointer to the mesh (level) object
    elem* el = _msh->el;  // pointer to the elem object in msh (level)

    const unsigned  dim = _msh->GetDimension(); // get the domain dimension of the problem
    unsigned iproc = _msh->processor_id(); // get the process_id (for parallel computation)
    unsigned nprocs = _msh->n_processors();

    std::vector < std::vector < double > > x(dim);    // local coordinates

    std::vector < int > xDof; // local to global pdeSys dofs

    MEDCoupling::MEDCouplingUMesh* mesh = MEDCoupling::MEDCouplingUMesh::New("Femus2MedMesh" + std::to_string(mshType), dim);

    mesh->allocateCells(_msh->_elementOffset[nprocs]);

    for (unsigned kproc = 0; kproc < nprocs; kproc++) {

      unsigned size = _msh->_elementOffset[kproc + 1] - _msh->_elementOffset[kproc];

      std::vector<unsigned> ielGeom(size);
      std::vector<int> nDofs(size);
      std::vector< mcIdType > topology;
      unsigned cnt = 0;

      if (iproc == kproc) {
        unsigned offset = _msh->_elementOffset[kproc];
        topology.resize(size * pow(3, dim));
        for (unsigned ii = 0; ii < size; ii++) {
          unsigned iel = ii + offset;
          ielGeom[ii] = _msh->GetElementType(iel);
          nDofs[ii]  = _msh->GetElementDofNumber(iel, mshType);
          for (unsigned i = 0; i < nDofs[ii]; i++) {
            topology[cnt]  = static_cast< mcIdType >(_msh->GetSolutionDof(i, iel, mshType));
            cnt++;
          }
        }
      }

      std::cout << " type of int = " << sizeof(mcIdType) << std::endl;
      MPI_Bcast(ielGeom.data(), size, MPI_UNSIGNED, kproc, MPI_COMM_WORLD);
      MPI_Bcast(nDofs.data(), size, MPI_INT, kproc, MPI_COMM_WORLD);
      MPI_Bcast(&cnt, 1, MPI_UNSIGNED, kproc, MPI_COMM_WORLD);
      topology.resize(cnt);
      if(sizeof(mcIdType) == 4)
        MPI_Bcast(topology.data(), cnt, MPI_INT, kproc, MPI_COMM_WORLD);
      else if(sizeof(mcIdType) == 8)
        MPI_Bcast(topology.data(), cnt, MPI_LONG, kproc, MPI_COMM_WORLD);
      else{
        std::cout << "this type int is not supported\n";
        abort();
      }
      cnt = 0;
      for (unsigned ii = 0; ii < size; ii++) {
        INTERP_KERNEL::NormalizedCellType cell = myFemusToMEDMapping[ielGeom[ii]][mshType];
        mesh->insertNextCell(cell, nDofs[ii], &topology[cnt]);
        cnt += nDofs[ii];
      }
    }
    mesh->finishInsertingCells();

    std::vector < NumericVector*> xP(dim);
    if (mshType == 2) {
      for (unsigned k = 0; k < dim; k++) xP[k] = _msh->_topology->_Sol[k];
    }
    else {
      for (unsigned k = 0; k < dim; k++) {
        xP[k] = NumericVector::build().release();
        if (nprocs == 1) {
          xP[k]->init(_msh->_dofOffset[mshType][nprocs],
                      _msh->_dofOffset[mshType][nprocs], false, SERIAL);
        }
        else {
          xP[k]->init(_msh->_dofOffset[mshType][nprocs],
                      _msh->_ownSize[mshType][iproc],
                      _msh->_ghostDofs[mshType][iproc], false, GHOSTED);
        }
        xP[k]->matrix_mult(*_msh->_topology->_Sol[k], *_msh->GetQitoQjProjection(mshType, 2));
      }
    }

    unsigned nvt = _msh->_dofOffset[mshType][nprocs];
    std::vector<double> xcoord(nvt * dim);
    unsigned cnt = 0;

    for (unsigned kproc = 0; kproc < nprocs; kproc++) {
      unsigned size = dim * (_msh->_dofOffset[mshType][kproc + 1] - _msh->_dofOffset[mshType][kproc]);
      if (kproc == iproc) {
        for (unsigned i = _msh->_dofOffset[mshType][iproc]; i < _msh->_dofOffset[mshType][iproc + 1]; i++) {
          for (unsigned k = 0; k < dim; k++) {
            xcoord[i * dim + k] = (*xP[k])(i);
          }
        }
      }
      MPI_Bcast(&xcoord[cnt], size, MPI_DOUBLE, kproc, MPI_COMM_WORLD);
      cnt += size;
    }

    MEDCoupling::DataArrayDouble* coordarr = MEDCoupling::DataArrayDouble::New();
    coordarr->alloc(nvt, dim);
    std::copy(xcoord.data(), xcoord.data() + xcoord.size(), coordarr->getPointer());
    mesh->setCoords(coordarr);
    mesh->zipCoords();

    _medMesh[mshType] = mesh;
    coordarr->decrRef();

    if (iproc == 0) {
      mesh->writeVTK(mesh->getName());
    }

    if (mshType != 2) {
      for (unsigned k = 0; k < dim; k++) delete xP[k];
    }

  }




  void FemusMedCoupling::Med2FemusNodeField(const std::vector<std::string> &subFieldName, const unsigned &mshType) {

    if (mshType > 2) {
      std::cout << "Error in void MyFEMuSMED::femus_to_med_field (const std::vector<std::string> &fieldName, const unsigned &mshType)!!!\n";
      std::cout << "Only linear \"0\", quadratic \"1\", or biquadratic \"2\" meshTypes are available\n";
      abort();
    }

    std::string allFieldName = "";
    for (unsigned i = 0; i < subFieldName.size(); i++) {
      allFieldName += subFieldName[i] + " ";
    }
    allFieldName += "on Femus2MedMesh" + std::to_string(mshType);

    unsigned cnt = _medField.size();
    for (unsigned i = 0; i < _medField.size(); i++) {
      if (_medField[i]->getName() == allFieldName) cnt = i;
    }

    if (cnt == _medField.size()) {
      std::cout << "Warning! " << allFieldName << " does not exist!\n";
      abort();
    }

    unsigned    iproc = _msh->processor_id(); // get the process_id (for parallel computation)
    unsigned    nprocs = _msh->n_processors();

    std::vector<unsigned> fieldIndex(subFieldName.size());
    std::vector<unsigned> fieldType(subFieldName.size());

    std::vector < NumericVector*> mysol(subFieldName.size());

    for (unsigned i = 0; i < subFieldName.size(); i++) {
      fieldIndex[i] = _sol->GetIndex(subFieldName[i].c_str());
      fieldType[i] =  _sol->GetSolutionType(fieldIndex[i]);
      if (fieldType[i] < 3) {
        if (fieldType[i] != mshType) {
          mysol[i] = NumericVector::build().release();
          if (nprocs == 1) {
            mysol[i]->init(_msh->_dofOffset[mshType][nprocs],
                           _msh->_dofOffset[mshType][nprocs], false, SERIAL);
          }
          else {
            mysol[i]->init(_msh->_dofOffset[mshType][nprocs],
                           _msh->_ownSize[mshType][iproc],
                           _msh->_ghostDofs[mshType][iproc], false, GHOSTED);
          }
        }
        else mysol[i] = _sol->_Sol[fieldIndex[i]];
      }
      else {
        std::cout << "Warning! " << subFieldName[i] << " is a cell field and cannot be projected into nodes. \n";
        abort();
      }
    }

    MEDCoupling::DataArrayDouble* fieldarr = _medField[cnt]->getArray();
    double *fieldarrayp = fieldarr->getPointer();

    for (unsigned i = 0; i < subFieldName.size(); i++) {
      _sol->_Sol[fieldIndex[i]]->zero();
    }

    for (unsigned i = _msh->_dofOffset[mshType][iproc]; i < _msh->_dofOffset[mshType][iproc + 1]; i++) {
      for (unsigned k = 0; k < subFieldName.size(); k++) {
        mysol[k]->set(i, fieldarrayp[i * subFieldName.size() + k]);
      }
    }

    for (unsigned k = 0; k < subFieldName.size(); k++)  mysol[k]->close();

    for (unsigned i = 0; i < subFieldName.size(); i++) {
      if (fieldType[i] != mshType) {
        _sol->_Sol[fieldIndex[i]]->matrix_mult(*mysol[i], *_msh->GetQitoQjProjection(fieldType[i], mshType));
        delete mysol[i];
      }
    }








  }


// =======================================================
  void FemusMedCoupling::Femus2MedNodeField(const std::vector<std::string> &fieldName, const unsigned &mshType) {

    if (mshType > 2) {
      std::cout << "Error in void MyFEMuSMED::femus_to_med_field (const std::vector<std::string> &fieldName, const unsigned &mshType)!!!\n";
      std::cout << "Only linear \"0\", quadratic \"1\", or biquadratic \"2\" meshTypes are available\n";
      abort();
    }


    if (_medMesh[mshType] == NULL) {
      this->Femus2MedMesh(mshType);
    }

    // Solution* sol = _mlSol.GetSolutionLevel(_numberOfUniformLevels - 1);
    // Mesh* msh = sol->GetMesh();    // pointer to the mesh (level) object
    unsigned    iproc = _msh->processor_id(); // get the process_id (for parallel computation)
    unsigned    nprocs = _msh->n_processors(); // get the process_id (for parallel computation)

    std::string fldAllName = "";
    //solution variable


    std::vector<unsigned> fieldIndex(fieldName.size());
    std::vector<unsigned> fieldType(fieldName.size());
    std::vector<std::string> thisFieldNames(fieldName.size());


    std::vector < NumericVector*> mysol(fieldName.size());

    unsigned fldSize = 0;
    for (unsigned i = 0; i < fieldName.size(); i++) {
      fieldIndex[fldSize] = _sol->GetIndex(fieldName[i].c_str());
      fieldType[fldSize] =  _sol->GetSolutionType(fieldIndex[fldSize]);
      if (fieldType[fldSize] < 3) {
        fldAllName += fieldName[i] + " ";
        thisFieldNames[fldSize] = fieldName[i];
        if (fieldType[fldSize] != mshType) {
          mysol[fldSize] = NumericVector::build().release();
          if (nprocs == 1) {
            mysol[fldSize]->init(_msh->_dofOffset[mshType][nprocs],
                                 _msh->_dofOffset[mshType][nprocs], false, SERIAL);
          }
          else {
            mysol[fldSize]->init(_msh->_dofOffset[mshType][nprocs],
                                 _msh->_ownSize[mshType][iproc],
                                 _msh->_ghostDofs[mshType][iproc], false, GHOSTED);
          }
          mysol[fldSize]->matrix_mult(*_sol->_Sol[fieldIndex[fldSize]], *_msh->GetQitoQjProjection(mshType, fieldType[fldSize]));
        }
        else mysol[fldSize] = _sol->_Sol[fieldIndex[fldSize]];
        fldSize++;
      }
      else {
        std::cout << "Warning! " << fieldName[i] << " is a cell field and cannot be projected into nodes. Removed from list \n";
      }
    }
    fldAllName += "on " + _medMesh[mshType]->getName();

    fieldIndex.resize(fldSize);
    fieldType.resize(fldSize);
    thisFieldNames.resize(fldSize);
    mysol.resize(fldSize);
    if (fldSize > 0) {

      unsigned nvt = _msh->_dofOffset[mshType][nprocs];
      std::vector<double> fielddata(nvt * fldSize);
      unsigned cnt = 0;

      for (unsigned kproc = 0; kproc < nprocs; kproc++) {
        unsigned size = fldSize * (_msh->_dofOffset[mshType][kproc + 1] - _msh->_dofOffset[mshType][kproc]);
        if (kproc == iproc) {
          for (unsigned i = _msh->_dofOffset[mshType][iproc]; i < _msh->_dofOffset[mshType][iproc + 1]; i++) {
            for (unsigned k = 0; k < fldSize; k++) {
              fielddata[i * fldSize + k] = (*mysol[k])(i);
            }
          }
        }
        MPI_Bcast(&fielddata[cnt], size, MPI_DOUBLE, kproc, MPI_COMM_WORLD);
        cnt += size;
      }

      for (unsigned k = 0; k < fldSize; k++) {
        if (fieldType[k] != mshType) {
          delete mysol[k];
        }
      }

      MEDCoupling::MEDCouplingFieldDouble *field = MEDCoupling::MEDCouplingFieldDouble::New(MEDCoupling::ON_NODES, MEDCoupling::ONE_TIME);

      field->setMesh(_medMesh[mshType]);
      field->setName(fldAllName);

      MEDCoupling::DataArrayDouble* fieldarr = MEDCoupling::DataArrayDouble::New();

      fieldarr->alloc(nvt, fldSize);
      std::copy(fielddata.data(), fielddata.data() + fielddata.size(), fieldarr->getPointer());
      field->setArray(fieldarr);
      field->checkConsistencyLight();
      field->setTime(0.0, 0, 0);
      fieldarr->decrRef();
      _medField.push_back(field);
      //_medFieldNames.push_back(thisFieldNames);
      if (iproc == 0) {
        field->writeVTK(field->getName());
      }
      std::cout << "projected fields: \n" << _medField[_medField.size() - 1]->getName() << std::endl;
      //for (unsigned k = 0; k < _medFieldNames[_medFieldNames.size() - 1].size(); k++) std::cout <<  _medFieldNames[_medFieldNames.size() - 1][k] << " ";
      //std::cout << std::endl;

    }
    else {
      std::cout << "Warning! There aren't node fields in the list! No fields projected!\n";
    }
  }

  MEDCouplingFieldDouble* FemusMedCoupling::GetFieldProjectionOnMesh(MEDCouplingFieldDouble *sourceField, MEDCouplingUMesh *med_target_mesh, const std::string & projName) {

    MEDCouplingRemapper remapper;
    remapper.setPrecision(1e-12);
    remapper.setIntersectionType(INTERP_KERNEL::Triangulation);
    remapper.prepare(sourceField->getMesh(), med_target_mesh, projName);

    sourceField->setNature(IntensiveMaximum);//Specify which formula to use in case of non overlapping meshes
    MEDCouplingFieldDouble *targetField = remapper.transferField(sourceField, 0.); //0. is the default value in case there is no overlapping

    //const std::vector<std::map<long int, double> >P = remapper.getCrudeMatrix();

    // for (unsigned i = 0; i < P.size(); i++) {
    //   for (std::map<long int, double>::const_iterator it = P[i].begin(); it != P[i].end(); ++it) {
    //     std::cout << it->first << ", " << it->second << "\t";
    //   }
    //   std::cout << std::endl;
    // }
    // std::cout << std::endl;
    // std::cout << std::endl;


    std::vector<std::string> subFieldName;
    GetSubFieldNames(sourceField->getName(), subFieldName);

    std::string allFieldName = "";
    for (unsigned i = 0; i < subFieldName.size(); i++) {
      allFieldName += subFieldName[i] + " ";
    }
    allFieldName += "on " + med_target_mesh->getName();

    targetField->setName(allFieldName);

    return targetField;

  }


  void FemusMedCoupling::BuildProjectionMatrixBetweenMeshes(const MEDCouplingUMesh *med_source_mesh, MEDCouplingUMesh *med_target_mesh) {


    MEDCouplingNormalizedUnstructuredMesh<2, 2> wrap_source_mesh(med_source_mesh);
    MEDCouplingNormalizedUnstructuredMesh<2, 2> wrap_target_mesh(med_target_mesh);
// Go for interpolation...
    INTERP_KERNEL::Interpolation2D myInterpolator;
    myInterpolator.setPrecision(1e-10);
    myInterpolator.setIntersectionType(INTERP_KERNEL::Triangulation);

    //Triangulation, Convex, Geometric2D, PointLocator, MappedBarycentric


    std::vector<std::map<int, double> > P;
    INTERP_KERNEL::Matrix<double, INTERP_KERNEL::ALL_C_MODE> resultMatrix2;


// here the interpolation is performed twice for this code to illustrate the possibility of storing data the interpolation matrix in 2 different data structures.
    myInterpolator.interpolateMeshes(wrap_source_mesh, wrap_target_mesh, P, "P1P1");
    myInterpolator.interpolateMeshes(wrap_source_mesh, wrap_target_mesh, resultMatrix2, "P1P1");

    for (unsigned i = 0; i < P.size(); i++) {
      for (std::map<int, double>::iterator it = P[i].begin(); it != P[i].end(); ++it) {
        std::cout << it->first << ", " << it->second << "\t";
      }
      std::cout << std::endl;
    }
    std::cout << std::endl;
    std::cout << std::endl;

    std::cout << resultMatrix2 << std::endl;
    std::cout << std::endl;
    std::cout << std::endl;

    unsigned ns = med_source_mesh->getNumberOfCells();
    unsigned nt = med_target_mesh->getNumberOfCells();

    std::vector<double> fieldDataSource(ns, 1.);
    std::vector<double> fieldDataTarget(nt);

    // resultMatrix2.multiply(fieldDataSource.data(),fieldDataTarget.data());
    //
    // std::cout << resultMatrix2 << std::endl;
    //
    //
    // std::cout << "number of rows in source mesh = " << ns << std::endl;
    // for(unsigned i = 0; i < ns;i++){
    //   std::cout << fieldDataSource[i] << " ";
    // }
    //
    // std::cout << "number of rows in target mesh = " << nt << std::endl;
    // for(unsigned i = 0; i < nt;i++){
    //   std::cout << fieldDataTarget[i] << " ";
    // }
    // std::cout<<std::endl;
//clean-up
//readerSource->Delete()
//readerTarget->Delete()
  }

  unsigned FemusMedCoupling::GetSubFieldPosition(const std::string &fieldName, const std::string &subFieldName) {

    unsigned start = 0;
    unsigned position = 0;
    bool subFieldFound = false;
    for (unsigned i = 0; i < fieldName.size(); i++) {
      if (" " == fieldName.substr(i, 1)) {
        if (subFieldName == fieldName.substr(start, i - start)) {
          subFieldFound = true;
          break;
        }
        else position++;
        i++;
        if ("on " == fieldName.substr(i, 3)) break;
        start = i;
      }
    }
    if (!subFieldFound) {
      std::cout << "Warning! No subfield with name " << subFieldName << " has been found on " << fieldName << "\n";
      abort();
    }
    return position;
  }

  void FemusMedCoupling::GetSubFieldNames(const std::string &fieldName, std::vector<std::string > &subFieldName) {
    subFieldName.resize(0);
    unsigned start = 0;
    bool subFieldFound = false;
    for (unsigned i = 0; i < fieldName.size(); i++) {
      if (" " == fieldName.substr(i, 1)) {
        subFieldName.push_back(fieldName.substr(start, i - start));
        i++;
        if ("on " == fieldName.substr(i, 3)) break;
        start = i;
      }
    }
  }




} // namespace femus

