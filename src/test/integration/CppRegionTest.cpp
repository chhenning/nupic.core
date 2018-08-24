/* ---------------------------------------------------------------------
 * Numenta Platform for Intelligent Computing (NuPIC)
 * Copyright (C) 2013, Numenta, Inc.  Unless you have an agreement
 * with Numenta, Inc., for a separate license for this software code, the
 * following terms and conditions apply:
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Affero Public License for more details.
 *
 * You should have received a copy of the GNU Affero Public License
 * along with this program.  If not, see http://www.gnu.org/licenses.
 *
 * http://numenta.org/licenses/
 * ---------------------------------------------------------------------
 */


#include <nupic/engine/NuPIC.hpp>
#include <nupic/engine/Network.hpp>
#include <nupic/engine/Region.hpp>
#include <nupic/engine/Spec.hpp>
#include <nupic/engine/Input.hpp>
#include <nupic/engine/Output.hpp>
#include <nupic/engine/Link.hpp>
#include <nupic/ntypes/Array.hpp>
#include <nupic/ntypes/ArrayRef.hpp>
#include <nupic/types/Exception.hpp>
#include <nupic/os/OS.hpp> // memory leak detection
#include <nupic/os/Env.hpp>
#include <nupic/os/Path.hpp>
#include <nupic/os/Timer.hpp>

#include <string>
#include <vector>
#include <cmath> // fabs/abs
#include <cstdlib> // exit
#include <iostream>
#include <stdexcept>

bool ignore_negative_tests = false;
#define SHOULDFAIL(statement) \
  { \
    if (!ignore_negative_tests) \
    { \
      bool caughtException = false; \
      try { \
        statement; \
      } catch(std::exception& ) { \
        caughtException = true; \
        std::cout << "Caught exception as expected: " # statement "" << std::endl;  \
      } \
      if (!caughtException) { \
        NTA_THROW << "Operation '" #statement "' did not fail as expected"; \
      } \
    } \
  }

using namespace nupic;

bool verbose = false;

struct MemoryMonitor
{
  MemoryMonitor()
  {
    OS::getProcessMemoryUsage(initial_vmem, initial_rmem);
  }

  ~MemoryMonitor()
  {
    if (hasMemoryLeaks())
    {
      NTA_DEBUG 
        << "Memory leaks detected. " 
        << "Real Memory: " << diff_rmem
        << ", Virtual Memory: " << diff_vmem;
    }
  }

  void update()
  {
    OS::getProcessMemoryUsage(current_vmem, current_rmem);
    diff_vmem = current_vmem - initial_vmem;
    diff_rmem = current_rmem - initial_rmem;
  }

  bool hasMemoryLeaks()
  {
    update();
    return diff_vmem > 0 || diff_rmem > 0;
  }

  size_t initial_vmem;
  size_t initial_rmem;
  size_t current_vmem;
  size_t current_rmem;
  size_t diff_rmem;
  size_t diff_vmem;
};


void testCppInputOutputAccess(Region_Ptr_t level1)
{
  // --- input/output access for level 1 (C++ TestNode) ---

  SHOULDFAIL( level1->getOutputData("doesnotexist") );

  // getting access via zero-copy
  std::cout << "Getting output for zero-copy access" << std::endl;
  ArrayRef output = level1->getOutputData("bottomUpOut");
  std::cout << "Element count in bottomUpOut is " << output.getCount() << "" << std::endl;
  Real64 *data_actual = (Real64*)output.getBuffer();
  // set the actual output
  data_actual[12] = 54321;  
}


void testCppLinking(std::string linkPolicy, std::string linkParams)
{
  Network net;

  Region_Ptr_t region1 = net.addRegion("region1", "TestNode", "");
  Region_Ptr_t region2 = net.addRegion("region2", "TestNode", "");
  net.link("region1", "region2", linkPolicy, linkParams);

  net.initialize();
          
  ArrayRef r1OutputArray = region1->getOutputData("bottomUpOut");

  region1->compute();
  std::cout << "Checking region1 output after first iteration..." << std::endl;
  Real64 *buffer = (Real64*) r1OutputArray.getBuffer();

  for (size_t i = 0; i < r1OutputArray.getCount(); i++)
  {
    if (verbose)
      std::cout << "  " << i << "    " << buffer[i] << "" << std::endl;
    if (i%2 == 0)
      NTA_CHECK(buffer[i] == 0);
    else
      NTA_CHECK(buffer[i] == (i-1)/2);
  }


  region2->prepareInputs();
  ArrayRef r2InputArray = region2->getInputData("bottomUpIn");
  std::cout << "Region 2 input after first iteration:" << std::endl;
  Real64 *buffer2 = (Real64*) r2InputArray.getBuffer();
  //NTA_CHECK(buffer != buffer2);

  for (size_t i = 0; i < r2InputArray.getCount(); i++)
  {
    if (verbose)
      std::cout << "  " << i << "    " << buffer2[i] << "" << std::endl;

    if (i%2 == 0)
      NTA_CHECK(buffer[i] == 0);
    else
      NTA_CHECK(buffer[i] == (i-1)/2);
  }

  // Now there can only be one node.  Node related tests removed.

}

void testYAML()  // Yaml for parameters.
{
  const char *params = "{int32Param: 1234, real64Param: 23.1}";
  //  badparams contains a non-existent parameter
  const char *badparams = "{int32Param: 1234, real64Param: 23.1, badParam: 4}";


  Network net = Network();
  Region_Ptr_t level1;
  SHOULDFAIL(level1 = net.addRegion("level1", "TestNode", badparams););
  
  level1 = net.addRegion("level1", "TestNode", params);
  net.initialize();

  // check default values
  Real32 r32val = level1->getParameterReal32("real32Param");
  NTA_CHECK(::fabs(r32val - 32.1) < 0.00001)  << "r32val = " << r32val << " diff = " << (r32val - 32.1);

  Int64 i64val = level1->getParameterInt64("int64Param");
  NTA_CHECK(i64val == 64) << "i64val = " << i64val;


  // check values set in region constructor
  Int32 ival = level1->getParameterInt32("int32Param");
  NTA_CHECK(ival == 1234) << "ival = " << ival;
  Real64 rval = level1->getParameterReal64("real64Param");
  NTA_CHECK(::fabs(rval - 23.1) < 0.00000000001) << "rval = " << rval;
  // TODO: if we get the real64 param with getParameterInt32 
  // it works -- should we flag an error?

  std::cout << "Got the correct values for all parameters set at region creation" << std::endl;

}

int realmain(bool leakTest)
{
  // verbose == true turns on extra output that is useful for
  // debugging the test (e.g. when the TestNode compute() 
  // algorithm changes)


  std::cout << "Creating network..." << std::endl;
  Network n;
  
  std::cout << "Region count is " << n.getRegions().getCount() << "" << std::endl;

  std::cout << "Adding a FDRNode region..." << std::endl;
  Region_Ptr_t level1 = n.addRegion("level1", "TestNode", "");

  std::cout << "Region count is " << n.getRegions().getCount() << "" << std::endl;
  std::cout << "Node type: " << level1->getType() << "" << std::endl;
  std::cout << "Nodespec is:\n"  << level1->getSpec()->toString() << "" << std::endl;
  
  Int64 val;
  Real64 rval;
  std::string int64Param("int64Param");
  std::string real64Param("real64Param");

  val = level1->getParameterInt64(int64Param);
  rval = level1->getParameterReal64(real64Param);
  std::cout << "level1.int64Param = " << val << "" << std::endl;
  std::cout << "level1.real64Param = " << rval << "" << std::endl;
    
  val = 20;
  level1->setParameterInt64(int64Param, val);
  val = 0;
  val = level1->getParameterInt64(int64Param);
  std::cout << "level1.int64Param = " << val << " after setting to 20" << std::endl;

  rval = 30.1;
  level1->setParameterReal64(real64Param, rval);
  rval = 0.0;
  rval = level1->getParameterReal64(real64Param);
  std::cout << "level1.real64Param = " << rval << " after setting to 30.1" << std::endl;

  // --- test getParameterInt64Array ---
  // Array a is not allocated by us. Will be allocated inside getParameter
  Array a(NTA_BasicType_Int64);
  level1->getParameterArray("int64ArrayParam", a);
  std::cout << "level1.int64ArrayParam size = " << a.getCount() << std::endl;
  std::cout << "level1.int64ArrayParam = [ ";
  Int64 * buff = (Int64 *)a.getBuffer();
  for (int i = 0; i < int(a.getCount()); ++i)
    std::cout << buff[i] << " ";
  std::cout << "]" << std::endl;
  
  // --- test setParameterInt64Array ---
  std::cout << "Setting level1.int64ArrayParam to [ 1 2 3 4 ]" << std::endl;
  std::vector<Int64> v(4);
  for (int i = 0; i < 4; ++i)
    v[i] = i+1  ;
  Array newa(NTA_BasicType_Int64, &v[0], v.size());
  level1->setParameterArray("int64ArrayParam", newa);

  // get the value of intArrayParam after the setParameter call.

  // The array a owns its buffer, so we can call releaseBuffer if we
  // want, but the buffer should be reused if we just pass it again. 
  // a.releaseBuffer();
  level1->getParameterArray("int64ArrayParam", a);
  std::cout << "level1.int64ArrayParam size = " << a.getCount() << std::endl;
  std::cout << "level1.int64ArrayParam = [ ";
  buff = (Int64 *)a.getBuffer();
  for (int i = 0; i < int(a.getCount()); ++i)
    std::cout << buff[i] << " ";
  std::cout << "]" << std::endl;

  n.run(1);
  n.initialize();   // extra initialized should be ignored.

  level1->compute();

  testCppInputOutputAccess(level1);
  testCppLinking("TestFanIn2", "");
  testCppLinking("UniformLink","{mapping: in, rfSize: [2]}");
  testYAML();

  std::cout << "Done -- all tests passed" << std::endl;

  return 0;
}

int main(int argc, char *argv[])
{
  
  /* 
   * Without arguments, this program is a simple end-to-end demo
   * of NuPIC 2 functionality, used as a developer tool (when 
   * we add a feature, we add it to this program. 
   * With an integer argument N, runs the same test N times
   * and requires that memory use stay constant -- it can't
   * grow by even one byte. 
   */

  // TODO: real argument parsing
  // Optional arg is number of iterations to do. 
  NTA_CHECK(argc == 1 || argc == 2);
  size_t count = 1;
  if (argc == 2)
  {
    std::stringstream ss(argv[1]);
    ss >> count;
  }
  // Start checking memory usage after this many iterations. 
#if defined(NTA_OS_WINDOWS)
  // takes longer to settle down on win32
  size_t memoryLeakStartIter = 6000;
#else
  size_t memoryLeakStartIter = 150;
#endif

  // This determines how frequently we check. 
  size_t memoryLeakDeltaIterCheck = 10;

  size_t minCount = memoryLeakStartIter + 5 * memoryLeakDeltaIterCheck;

  if (count > 1 && count < minCount)
  {
    std::cout << "Run count of " << count << " specified\n";
    std::cout << "When run in leak detection mode, count must be at least " << minCount << "\n";
    ::exit(1);
  }
   

  size_t initial_vmem = 0;
  size_t initial_rmem = 0;
  size_t current_vmem = 0;
  size_t current_rmem = 0;
  try {
    for (size_t i = 0; i < count; i++)
    {
      //MemoryMonitor m;
      NuPIC::init();
      realmain(count > 1);
      //testExceptionBug();
      //testCppLinking("TestFanIn2","");
      NuPIC::shutdown();
      // memory leak detection
      // we check even prior to the initial tracking iteration, because the act
      // of checking potentially modifies our memory usage
      if (i % memoryLeakDeltaIterCheck == 0)
      {
        OS::getProcessMemoryUsage(current_rmem, current_vmem);
        if(i == memoryLeakStartIter)
        {
          initial_rmem = current_rmem;
          initial_vmem = current_vmem;
        }
        std::cout << "Memory usage: " << current_vmem << " (virtual) " 
                  << current_rmem << " (real) at iteration " << i << std::endl;

        if(i >= memoryLeakStartIter)
        {
          if (current_vmem > initial_vmem || current_rmem > initial_rmem)
          {
            std::cout << "Tracked memory usage (iteration "
                      << memoryLeakStartIter << "): " << initial_vmem
                      << " (virtual) " << initial_rmem << " (real)" << std::endl;
            throw std::runtime_error("Memory leak detected");
          }
        }
      }
    }

  } catch (nupic::Exception& e) {
    std::cout 
      << "Exception: " << e.getMessage() 
      << " at: " << e.getFilename() << ":" << e.getLineNumber()
      << std::endl;
    return 1;

  } catch (std::exception& e) {
    std::cout << "Exception: " << e.what() << "" << std::endl;
    return 1;
  }
  catch (...) {
    std::cout << "\nHtmTest is exiting because an exception was thrown" << std::endl;
    return 1;
  }
  if (count > 20)
    std::cout << "Memory leak check passed -- " << count << " iterations" << std::endl;

  std::cout << "--- ALL TESTS PASSED ---" << std::endl;
  return 0;
}
