#include <schedulerproject.hh>

using namespace OpenHRP;
using namespace std;
using namespace hrp;

#if 0
#define ODEBUG(X)
#else
#define ODEBUG(X) X
#endif

#define ODEBUG3(X)

template <typename X, typename X_ptr>
X_ptr checkCorbaServer(std::string n, CosNaming::NamingContext_var &cxt)
{
  CosNaming::Name ncName;
  ncName.length(1);
  ncName[0].id = CORBA::string_dup(n.c_str());
  ncName[0].kind = CORBA::string_dup("");
  X_ptr srv = NULL;
  try {
    srv = X::_narrow(cxt->resolve(ncName));
  } catch(const CosNaming::NamingContext::NotFound &exc) {
    std::cerr << n << " not found: ";
    switch(exc.why) {
    case CosNaming::NamingContext::missing_node:
      std::cerr << "Missing Node" << std::endl;
    case CosNaming::NamingContext::not_context:
      std::cerr << "Not Context" << std::endl;
      break;
    case CosNaming::NamingContext::not_object:
      std::cerr << "Not Object" << std::endl;
      break;
    }
    return (X_ptr)NULL;
  } catch(CosNaming::NamingContext::CannotProceed &exc) {
    std::cerr << "Resolve " << n << " CannotProceed" << std::endl;
  } catch(CosNaming::NamingContext::AlreadyBound &exc) {
    std::cerr << "Resolve " << n << " InvalidName" << std::endl;
  }
  return srv;
}


SchedulerProject::SchedulerProject()
{
  simulationData_ = 
    std::make_shared<SimulationItem>();
  aListOfModelItems_ = 
    std::make_shared< std::vector<ModelItem> >();
  aListOfCollisionPairItems_ = 
    std::make_shared< std::vector<CollisionPairItem> > ();

}

SchedulerProject::~SchedulerProject()
{
}

void SchedulerProject::parseOptions(int argc, char *argv[])
{
  std::string ProjectFname;
  
  for (int i=1; i<argc; i++)
    {
      if (strcmp("-ORBconfig", argv[i])==0 || strcmp("-ORBInitRef", argv[i])==0 )
	{
	  ++i;	// skip ORB parameter
	}
      else if (strcmp("-url", argv[i])==0)
	{
	  ProjectFname = argv[++i];
	}
      else if (strcmp("-timeStep", argv[i])==0)
	{
	  simulationData_.get()->timeStep = atof(argv[++i]);
	}
      
    }
  
  
  projectfilename_ = "file://" + ProjectFname;
  ODEBUG(std::cout << "Model: " << projectfilename_ << std::endl);
}

void SchedulerProject::loadModels(int argc,char * argv[])
{
  // Create the list of logs.
  listOfLogs_.resize(aListOfModelItems_.get()->size());

  for(unsigned int i=0;i<aListOfModelItems_.get()->size();i++)
    {
      // Create a body info structure.
      (*aListOfModelItems_.get())[i].bodyInfoVar = 
	hrp::loadBodyInfo((*aListOfModelItems_.get())[i].url.c_str(), 
			  argc, argv);
      if (!(*aListOfModelItems_.get())[i].bodyInfoVar )
	{
	  cerr << "ModelLoader: " 
	       << (*aListOfModelItems_.get())[i].url
	       << " cannot be loaded" << endl;
	  return;
	  
	}
      // Create a log object.
      std::string directory("/tmp/");
      listOfLogs_[i].setNameAndPath((*aListOfModelItems_.get())[i].name,
				     directory);
     }
 }

 void SchedulerProject::initCorba(int argc, char * argv[])
 {
   // initialize CORBA 
   CORBA::ORB_var orb;
   orb = CORBA::ORB_init(argc, argv);

   // ROOT POA
   CORBA::Object_var poaObj = orb -> resolve_initial_references("RootPOA");
   PortableServer::POA_var rootPOA = PortableServer::POA::_narrow(poaObj);

   // get reference to POA manager
   PortableServer::POAManager_var manager = rootPOA -> the_POAManager();


   {
     CORBA::Object_var	nS = orb->resolve_initial_references("NameService");
     cxt_ = CosNaming::NamingContext::_narrow(nS);
   }
 }

 void SchedulerProject::initOLV(int argc, char * argv[])
 {
   olv_ = hrp::getOnlineViewer(argc, argv);

   if (CORBA::is_nil( olv_ )) 
     {
       std::cerr << "OnlineViewer not found" << std::endl;
       return;
     }
   try 
     {
       for(unsigned int i=0;i<aListOfModelItems_.get()->size();i++)
	 {
	   olv_->load((*aListOfModelItems_.get())[i].bodyInfoVar->name(),
		      (*aListOfModelItems_.get())[i].url.c_str());
	 }

       olv_->clearLog();
     } 
   catch (CORBA::SystemException& ex) 
     {
       cerr << "Failed to connect GrxUI." << endl;
       return;
     }
 }


 void SchedulerProject::setBodyAngle(JointData &aJointData,
				     OpenHRP::BodyInfo_var  aBodyInfo,
				     CORBA::String_var CORBAbodyName)
 {
   // Set angle value.
   DblSequence corba_angle;
   corba_angle.length(1);
   corba_angle[0] = aJointData.angle;
   dynamicsSimulator_->setCharacterLinkData( aBodyInfo->name(), 
					     CORBAbodyName,
					     DynamicsSimulator::JOINT_VALUE, 
					     corba_angle );
 }

 void SchedulerProject::setBodyVelocity(JointData &aJointData,
					OpenHRP::BodyInfo_var  aBodyInfo,
					CORBA::String_var CORBAbodyName)
 {

   if ((aJointData.velocity.size()>0) && 
       (aJointData.angularVelocity.size()>0))
     {
       DblSequence   velocity; velocity.length(6);
       for(unsigned int idVel=0;idVel<3;idVel++)
	 { velocity[idVel] = aJointData.velocity[idVel];
	   velocity[idVel+3] = aJointData.angularVelocity[idVel];
	 }
       dynamicsSimulator_->setCharacterLinkData( aBodyInfo->name(), 
						 CORBAbodyName,
						 DynamicsSimulator::ABS_VELOCITY, 
						 velocity );
     }
 }

 struct AxisAngle4f
 {
   double x,y,z,angle;
 };

 struct rotationMatrix3d
 {
   double m00, m01, m02, m10, m11, m12, m20, m21, m22;
   constexpr static double EPS = 1.110223024E-16;

   void fromAxisAngle4(AxisAngle4f &a1)
   {
     double mag = sqrt( a1.x*a1.x + a1.y*a1.y + a1.z*a1.z);

     if( mag < EPS ) {
       m00 = 1.0;
       m01 = 0.0;
       m02 = 0.0;

       m10 = 0.0;
       m11 = 1.0;
       m12 = 0.0;

       m20 = 0.0;
       m21 = 0.0;
       m22 = 1.0;
     } else {
       mag = 1.0/mag;
       double ax = a1.x*mag;
       double ay = a1.y*mag;
       double az = a1.z*mag;

       double sinTheta = sin(a1.angle);
       double cosTheta = cos(a1.angle);
       double t = 1.0 - cosTheta;

       double xz = ax * az;
       double xy = ax * ay;
       double yz = ay * az;

       m00 = t * ax * ax + cosTheta;
       m01 = t * xy - sinTheta * az;
       m02 = t * xz + sinTheta * ay;

       m10 = t * xy + sinTheta * az;
       m11 = t * ay * ay + cosTheta;
       m12 = t * yz - sinTheta * ax;

       m20 = t * xz - sinTheta * ay;
       m21 = t * yz + sinTheta * ax;
       m22 = t * az * az + cosTheta;
     }
   }
 };

void SchedulerProject::setBodyAbsolutePosition(JointData &aJointData,
					       OpenHRP::BodyInfo_var  aBodyInfo,
					       CORBA::String_var CORBAbodyName)
{
  ODEBUG3(std::cout << "Going through setBodyAbsolute Position for :" 
	  << aBodyInfo->name() << " " 
	  << CORBAbodyName << " " 
	  << aJointData.translation.size() << " "
	  << aJointData.rotation.size()
	  << std::endl);
  if ((aJointData.translation.size()>0) && 
      (aJointData.rotation.size()>0))
    {
      ODEBUG3(std::cout << "initialize absolute position of :"
	     << aBodyInfo->name() << " link: " << CORBAbodyName << std::endl);
	     
      DblSequence TransformArray; TransformArray.length(12);
      for(unsigned int i=0;i<3;i++)
	TransformArray[i] = aJointData.translation[i];
      
      AxisAngle4f anAxisAngle;
      anAxisAngle.x = aJointData.rotation[0];
      anAxisAngle.y = aJointData.rotation[1];
      anAxisAngle.z = aJointData.rotation[2];
      anAxisAngle.angle = aJointData.rotation[3];
      rotationMatrix3d aRotation;
      aRotation.fromAxisAngle4(anAxisAngle);
      TransformArray[3] = aRotation.m00;TransformArray[4]  = aRotation.m01;TransformArray[5]  = aRotation.m02;
      TransformArray[6] = aRotation.m10;TransformArray[7]  = aRotation.m11;TransformArray[8]  = aRotation.m12;
      TransformArray[9] = aRotation.m20;TransformArray[10] = aRotation.m21;TransformArray[11] = aRotation.m22;
      dynamicsSimulator_->setCharacterLinkData( aBodyInfo->name(), 
						CORBAbodyName,
						DynamicsSimulator::ABS_TRANSFORM, 
						TransformArray );
      ODEBUG(for(unsigned int i=0;i<4;i++) 
	       {
		 for(unsigned int j=0;j<3;j++)
		   std::cout << TransformArray[i*3+j] << " ";
		 std::cout << std::endl;
	       }
	     
	     );
    }
}

void SchedulerProject::setBodyMode(JointData &aJointData,
				    OpenHRP::BodyInfo_var  aBodyInfo,
				    CORBA::String_var CORBAbodyName)
{
  DblSequence wdata;
  wdata.length(1);
  ODEBUG(std::cout << "In " << aBodyInfo->name() << "setBodyMode for " << CORBAbodyName);
  if (aJointData.mode==0)
    {
      wdata[0] = 0.0;
      dynamicsSimulator_->setCharacterLinkData( aBodyInfo->name(), 
						CORBAbodyName, 
						DynamicsSimulator::POSITION_GIVEN, 
						wdata ); 
      ODEBUG(std::cout << ": High Gain ");
    }
  else 
    {
      wdata[0] = -1.0;
      dynamicsSimulator_->setCharacterLinkData( aBodyInfo->name(), 
						CORBAbodyName, 
						DynamicsSimulator::POSITION_GIVEN, 
						wdata ); 
      ODEBUG(std::cout << ": Torque ");
    }
  ODEBUG(std::cout << std::endl);
}

void SchedulerProject::initRobotsJointData()
{
  if (simulationData_.get()->integrate)
    {
      // Iterate over the robots.
      for(unsigned int idModel=0;
	  idModel < aListOfModelItems_.get()->size();
	  idModel++)
	{
	  OpenHRP::BodyInfo_var aBodyInfo = 
	    (*aListOfModelItems_.get())[idModel].bodyInfoVar;
	  LinkInfoSequence_var aLinkInfoSequence = aBodyInfo->links();
	 
	  // Iterave over the links
	  for(unsigned int idLinks=0;
	      idLinks<aLinkInfoSequence->length();
	      idLinks++)
	    {
	      CORBA::String_var CORBAbodyName = 
		aLinkInfoSequence[idLinks].name;
	      std::string bodyName(CORBAbodyName);
	      
	      JointData aJointData = 
		(*aListOfModelItems_.get())[idModel].jointsMap[bodyName];
	      setBodyAbsolutePosition(aJointData,aBodyInfo,CORBAbodyName);  // Set Body absoluteposition.
	      setBodyAngle(aJointData,aBodyInfo,CORBAbodyName);     // Set Body angle value.
	      setBodyVelocity(aJointData,aBodyInfo,CORBAbodyName);  // Set Joint speed.
	      setBodyMode(aJointData,aBodyInfo,CORBAbodyName);  // Set Joint mode.
	    }
	}
    }
    
}

void SchedulerProject::initParallelMecanisms()
{
  // TO DO EXTRACT EXTRA JOINTS
}

void SchedulerProject::initCollisions()
{
  ODEBUG(std::cout << "Start initCollisions" << std::endl);
  // Iterate over all the collision pair.
  for(unsigned int idCollision=0;
      idCollision<aListOfCollisionPairItems_.get()->size();
      idCollision++)
    {
      CollisionPairItem & aCollisionPI = 
	(*aListOfCollisionPairItems_.get())[idCollision];
      // static/slip friction coefficient 
      double statFric,slipFric;
      statFric = aCollisionPI.staticFriction;
      slipFric = aCollisionPI.slidingFriction;
      double culling_thresh = aCollisionPI.cullingThresh;
      ODEBUG(std::cout << "staticFriction: "<< statFric 
	     << " slipFriction: "<< slipFric 
	     << " cullingThres: "<< culling_thresh
	     << std::endl);
      // Take into accound spring damper if specified
      // usually not a good idea.
      DblSequence6 K, C;    
      if (aCollisionPI.springDamperModel)
	{
	  K.length((CORBA::ULong)aCollisionPI.springConstant.size());
	  C.length((CORBA::ULong)aCollisionPI.damperConstant.size());
	  for(unsigned int i=0;i<aCollisionPI.springConstant.size();i++)
	    { K[i] = aCollisionPI.springConstant[i];
	      ODEBUG(std::cout << "K["<<i<<"]="<<K[i] << std::endl);
	    }
	  for(unsigned int i=0;i<aCollisionPI.damperConstant.size();i++)
	    { C[i] = aCollisionPI.damperConstant[i];
	      ODEBUG(std::cout << "C["<<i<<"]="<<C[i] << std::endl);
	    }
	}
      else 
	{
	  K.length(0); C.length(0);
	}
      ODEBUG(std::cout << "K.length()=" << K.length() << " " 
	     << "C.lenght()=" << C.length() << std::endl);
      ODEBUG(std::cout << "Register collision pair:" 
	     << aCollisionPI.objectName1.c_str() << "|"
	     << aCollisionPI.jointName1.c_str() << "|" 
	     << aCollisionPI.objectName2.c_str() << "|"
	     << aCollisionPI.jointName2.c_str() << "|"
	     << std::endl);
      dynamicsSimulator_->
	registerCollisionCheckPair(aCollisionPI.objectName1.c_str(),
				   aCollisionPI.jointName1.c_str(),
				   aCollisionPI.objectName2.c_str(),
				   aCollisionPI.jointName2.c_str(),
				   statFric,
				   slipFric,
				   K,C,culling_thresh,0.0);
      
	
    }
  ODEBUG(std::cout << "End of initCollisions" << std::endl);
  
}
void SchedulerProject::initDynamicsSimulator()
{
  DynamicsSimulatorFactory_var dynamicsSimulatorFactory;
  dynamicsSimulatorFactory =
    checkCorbaServer <DynamicsSimulatorFactory, DynamicsSimulatorFactory_var> ("DynamicsSimulatorFactory", cxt_);
  
  if (CORBA::is_nil(dynamicsSimulatorFactory)) {
    std::cerr << "DynamicsSimulatorFactory not found" << std::endl;
  }
  
  // Create dynamics simulator
  dynamicsSimulator_ = dynamicsSimulatorFactory->create();
  
  // Register robots.
  cout << "** Dynamics server setup ** " << endl;
  for(unsigned int i=0;i<aListOfModelItems_.get()->size();i++)
    {
      OpenHRP::BodyInfo_var aBodyInfo = (*aListOfModelItems_.get())[i].bodyInfoVar;
      
      cout << "Character  : |" << aBodyInfo->name() << "|" << std::endl;
      dynamicsSimulator_->registerCharacter(aBodyInfo->name(), 
					    aBodyInfo);			    
    }

  // Enable sensor and gravity.
  CORBA::Double ltimestep = simulationData_.get()->timeStep;
  dynamicsSimulator_->init(ltimestep, 
			   simulationData_.get()->method,
			   DynamicsSimulator::ENABLE_SENSOR);
  DblSequence3 g;
  g.length(3);
  g[0] = 0.0;
  g[1] = 0.0;
  double world_gravity = simulationData_.get()->gravity;  
  // default gravity acceleration [m/s^2]
  g[2] = world_gravity;
  dynamicsSimulator_->setGVector(g);
  
  //initRobotsPose();
  initRobotsJointData();
  dynamicsSimulator_->calcWorldForwardKinematics();
  
  initCollisions();
  initParallelMecanisms();
  dynamicsSimulator_->initSimulation();

}

void SchedulerProject::initController()
{
  // Allocate the number of possible controllers.
  aListOfControllers_.resize(aListOfModelItems_.get()->size());

  for(unsigned int i=0;i<aListOfModelItems_.get()->size();i++)
    {
      
      std::string &lControllerName =  (*aListOfModelItems_.get())[i].controllerName;
      bool lcontrollerOpenHRP;
      
      // If no controller is specified
      if (lControllerName.length()==0)
	{
	  aListOfControllers_[i].controller_found = false;
	  continue; // Go to the next candidate
	}
      
      try 
	{
	  aListOfControllers_[i].controllerName = lControllerName;
	  aListOfControllers_[i].controller = 
	    checkCorbaServer <Controller, Controller_var> 
	    (lControllerName.c_str(), cxt_);
	  aListOfControllers_[i].controller_found = true;
	}
      catch (CORBA::SystemException& ex) 
	{
	  cerr << "Failed to connect to " << lControllerName.c_str() << endl;
	  aListOfControllers_[i].controller_found = false;
	}
      
      if (CORBA::is_nil(aListOfControllers_[i].controller)) {
	std::cerr << "Controller " << lControllerName << " not found" << std::endl;
	aListOfControllers_[i].controller_found = false;
      }

      if (aListOfControllers_[i].controller_found)
	{
	  // Set the character/model name used by the controller.
	  aListOfControllers_[i].controller->
	    setModelName(CORBA::string_dup((*aListOfModelItems_.get())[i].name.c_str()));
	  // Set the dynamics simulator
	  aListOfControllers_[i].controller->
	    setDynamicsSimulator(dynamicsSimulator_);
	  // Initialize the controller
	  aListOfControllers_[i].controller->initialize();
	  // Specify the time
	  aListOfControllers_[i].controller->setTimeStep(controlTimeStep_);
	  // State the controller
	  aListOfControllers_[i].controller->start();
	}
    }

}

void SchedulerProject::init(int argc,char * argv[])
{
  
  // Parse options
  parseOptions(argc,argv);

  // ================== Project Load ===============================
  aProjectLoader_.loadProject(projectfilename_,
			     simulationData_,
			     aListOfModelItems_,
			     aListOfCollisionPairItems_);

  //================== Model Load ===============================
  loadModels(argc,argv);
  
  //================== CORBA init ===============================
  initCorba(argc,argv);
  
  //==================== OnlineViewer (GrxUI) setup ===============
  initOLV(argc,argv);
  
  //================= DynamicsSimulator setup ======================
  initDynamicsSimulator();
  
  // ==================  Controller setup ==========================
  initController();
}

void SchedulerProject::mainLoop()
{
  // ==================  log file   ======================
  static ofstream log_file;
  log_file.open("samplePD.log");
  
  WorldState_var state;
  int i=0;
  int j = 0;
  double time=0.0;
  double controlTime=0.0;

  while ( 1 ) {
    bool control=false;
    if(controlTime <= time){
      control=true;
      j++;
    }

    if(control)
      {
	for(unsigned int i=0;i<aListOfControllers_.size();i++)
	  {
	    if (aListOfControllers_[i].controller_found)
	      aListOfControllers_[i].controller->input();
	  }
      }

    i++;
    if (i%1000==0)
      std::cout << " Counter: " << i << std::endl;

    time = simulationData_.get()->timeStep * i;
    controlTime = controlTimeStep_ * j;

    if(control)
      {
	for(unsigned int i=0;i<aListOfControllers_.size();i++)
	  {
	    if (aListOfControllers_[i].controller_found)
	      aListOfControllers_[i].controller->control();
	  }
      }
      
    // ================== simulate one step ==============
    dynamicsSimulator_->stepSimulation();					
               
    // ================== viewer update ====================
    try {
      dynamicsSimulator_ -> getWorldState( state );
      olv_->update( state );
    } catch (CORBA::SystemException& ex) {
      return ;
    }

    // ================== call control =====================
    if(control)
      {
	for(unsigned int i=0;i<aListOfControllers_.size();i++)
	  {
	    if (aListOfControllers_[i].controller_found)
	      aListOfControllers_[i].controller->output();
	  }

      }
    // ================== save log =====================
    saveLog(time);

    if( time > simulationData_.get()->totalTime ) break;
    
  }

  //controller->stop();
  log_file.close();
  dynamicsSimulator_->destroy();

}

void SchedulerProject::saveLog(double ltime)
{
  // Iterate over all the characters loaded 
  for(unsigned int idModel=0;
      idModel<listOfLogs_.size();
      idModel++)
    {
      OpenHRP::SensorState_var sstate;
      OpenHRP::BodyInfo_var aBodyInfo = 
	(*aListOfModelItems_.get())[idModel].bodyInfoVar;
      
      dynamicsSimulator_->getCharacterSensorState(aBodyInfo->name(), 
						 sstate);
      for(unsigned int aLinkDataType=(unsigned int)
	    DynamicsSimulator::JOINT_VALUE;
	  aLinkDataType<=(unsigned int)DynamicsSimulator::JOINT_TORQUE;
	  aLinkDataType++)
	{
	  unsigned int sizeofdata[7] = {1,1,1,1,12,6,6};
	  LinkInfoSequence_var aLinkInfo = aBodyInfo->links();
	  std::vector<double>  & avec_data=
	    listOfLogs_[idModel].jointData[aLinkDataType-
					   DynamicsSimulator::JOINT_VALUE];
	  avec_data.resize(aLinkInfo->length()*sizeofdata[aLinkDataType]);

	  for(unsigned int idLink=0;idLink<aLinkInfo->length();idLink++)
	    {
	      DblSequence_var wdata;
	      dynamicsSimulator_->getCharacterLinkData(aBodyInfo->name(),
						       aLinkInfo[idLink].name,
						       (DynamicsSimulator::LinkDataType)aLinkDataType,
						       wdata);
	      for(unsigned int i=0;i<wdata->length();i++)
		avec_data[sizeofdata[aLinkDataType]*idLink+i] = wdata[i];
	    }
	}
      listOfLogs_[idModel].saveLog(ltime,sstate);
    }
}