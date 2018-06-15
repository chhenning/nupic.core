/* ---------------------------------------------------------------------
 * Numenta Platform for Intelligent Computing (NuPIC)
 * Copyright (C) 2017, Numenta, Inc.  Unless you have an agreement
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

/** @file
 * Implementation of Link test
 */

#include <sstream>

#include "gtest/gtest.h"

#include <nupic/engine/Input.hpp>
#include <nupic/engine/Network.hpp>
#include <nupic/engine/Output.hpp>
#include <nupic/engine/Region.hpp>
#include <nupic/engine/RegionImpl.hpp>
#include <nupic/engine/RegionImplFactory.hpp>
#include <nupic/engine/RegisteredRegionImpl.hpp>
#include <nupic/engine/Spec.hpp>
#include <nupic/engine/TestNode.hpp>
#include <nupic/ntypes/Array.hpp>
#include <nupic/ntypes/BundleIO.hpp>
#include <nupic/os/Directory.hpp>
#include <nupic/types/ptr_types.hpp>
#include <nupic/utils/Log.hpp>

using namespace nupic;

TEST(LinkTest, Links) {
  Network net;
  auto region1 = net.addRegion("region1", "TestNode", "");
  auto region2 = net.addRegion("region2", "TestNode", "");

  net.link("region1", "region2", "", "");

  // test initialize(), which is called by net.initialize()
  // also test evaluateLinks() which is called here
  net.initialize();
  net.run(1);


  // test getName() and setName()
  Input *in1 = region1->getInput("bottomUpIn");
  Input *in2 = region2->getInput("bottomUpIn");

  EXPECT_STREQ("bottomUpIn", in1->getName().c_str());
  EXPECT_STREQ("bottomUpIn", in2->getName().c_str());
  in1->setName("uselessName");
  EXPECT_STREQ("uselessName", in1->getName().c_str());
  in1->setName("bottomUpIn");

  // test isInitialized()
  ASSERT_TRUE(in1->isInitialized());
  ASSERT_TRUE(in2->isInitialized());

  // test getLinks()
  std::vector<Link_Ptr_t> links = in2->getLinks();
  ASSERT_EQ(1u, links.size());
  for (auto &link : links) {
    // do something to make sure l[i] is a valid Link*
    ASSERT_TRUE(link != nullptr);
    // should fail because regions are initialized
    EXPECT_THROW(in2->removeLink(link), std::exception);
  }

  // test findLink()
  auto l1 = in1->findLink("region1", "bottomUpOut");
  ASSERT_TRUE(l1 == nullptr);
  auto l2 = in2->findLink("region1", "bottomUpOut");
  ASSERT_TRUE(l2 != nullptr);

  // test removeLink(), uninitialize()
  // uninitialize() is called internally from removeLink()
  {
    // can't remove link b/c region1 initialized
    EXPECT_THROW(in2->removeLink(l2), std::exception);
    // can't remove region b/c region1 has links
    EXPECT_THROW(net.removeRegion("region1"), std::exception);
    region1->uninitialize();
    region2->uninitialize();
    EXPECT_THROW(in1->removeLink(l2), std::exception);
    in2->removeLink(l2);
    EXPECT_THROW(in2->removeLink(l2), std::exception);
    // l1 == NULL
    EXPECT_THROW(in1->removeLink(l1), std::exception);
  }
}

TEST(LinkTest, DelayedLink) {
  class MyTestNode : public TestNode {
  public:
    MyTestNode(const ValueMap &params, Region *region)
        : TestNode(params, region) {}

    MyTestNode(BundleIO &bundle, Region *region) : TestNode(bundle, region) {}

    std::string getNodeType() { return "MyTestNode"; }

    void compute() override {
      // Replace with no-op to preserve output
    }
  };

  RegionImplFactory::registerCPPRegion("MyTestNode",
                                       new RegisteredRegionImpl<MyTestNode>());

  Network net;
  auto region1 = net.addRegion("region1", "MyTestNode", "{count: 64}");
  auto region2 = net.addRegion("region2", "TestNode", "");

  RegionImplFactory::unregisterCPPRegion("MyTestNode");

  // NOTE: initial delayed values are set to all 0's
  net.link("region1", "region2", "", "", "", "",
           2 /*propagationDelay*/);

  // test initialize(), which is called by net.initialize()
  net.initialize();

  Input *in1 = region1->getInput("bottomUpIn");
  Input *in2 = region2->getInput("bottomUpIn");
  Output *out1 = region1->getOutput("bottomUpOut");

  // test isInitialized()
  ASSERT_TRUE(in1->isInitialized());
  ASSERT_TRUE(in2->isInitialized());


  // set in2 to all 1's, to detect if net.run fails to update the input.
  {
    const ArrayBase *ai2 = &(in2->getData());
    ASSERT_EQ(ai2->getCount(), 64);
    Real64 *idata = (Real64 *)(ai2->getBuffer());
    for (UInt i = 0; i < 64; i++)
      idata[i] = 1;
  }

  // set out1 to all 10's
  {
    const ArrayBase *ao1 = &(out1->getData());
    ASSERT_EQ(ao1->getCount(), 64);
    Real64 *idata = (Real64 *)(ao1->getBuffer());
    for (UInt i = 0; i < 64; i++)
      idata[i] = 10;
  }

  // Check extraction of first delayed value
  {
    // This run should also pick up the 10s
    net.run(1);

    // confirm that in2 is all zeroes
    const ArrayBase *ai2 = &(in2->getData());
    ASSERT_EQ(ai2->getCount(), 64);
    Real64 *idata = (Real64 *)(ai2->getBuffer());
    // only test 4 instead of 64 to cut down on number of tests
    for (UInt i = 0; i < 4; i++)
      ASSERT_EQ(0, idata[i]);
  }

  // set out1 to all 100's
  {
    const ArrayBase *ao1 = &(out1->getData());
    ASSERT_EQ(ao1->getCount(), 64);
    Real64 *idata = (Real64 *)(ao1->getBuffer());
    for (UInt i = 0; i < 64; i++)
      idata[i] = 100;
  }

  // Check extraction of second delayed value
  {
    net.run(1);

    // confirm that in2 is all zeroes
    const ArrayBase *ai2 = &(in2->getData());
    ASSERT_EQ(ai2->getCount(), 64);
    Real64 *idata = (Real64 *)(ai2->getBuffer());
    // only test 4 instead of 64 to cut down on number of tests
    for (UInt i = 0; i < 4; i++)
      ASSERT_EQ(0, idata[i]);
  }

  // Check extraction of first "generated" value
  {
    net.run(1);

    // confirm that in2 is now all 10's
    const ArrayBase *ai2 = &(in2->getData());
    ASSERT_EQ(ai2->getCount(), 64);
    Real64 *idata = (Real64 *)(ai2->getBuffer());
    // only test 4 instead of 64 to cut down on number of tests
    for (UInt i = 0; i < 4; i++)
      ASSERT_EQ(10, idata[i]);
  }

  // Check extraction of second "generated" value
  {
    net.run(1);

    // confirm that in2 is now all 100's
    const ArrayBase *ai2 = &(in2->getData());
    ASSERT_EQ(ai2->getCount(), 64);
    Real64 *idata = (Real64 *)(ai2->getBuffer());
    // only test 4 instead of 64 to cut down on number of tests
    for (UInt i = 0; i < 4; i++)
      ASSERT_EQ(100, idata[i]);
  }
}

TEST(LinkTest, DelayedLinkSerialization) {
  // serialization test of delayed link.

  // create an subclass of TestNode plugin
  class MyTestNode : public TestNode {
  public:
    MyTestNode(const ValueMap &params, Region *region)
        : TestNode(params, region) {}

    MyTestNode(BundleIO &bundle, Region *region) : TestNode(bundle, region) {}

    std::string getNodeType() { return "MyTestNode"; };

    void compute() override {
      // Replace with no-op to preserve output
    }
  };
  // Register the plugin
  RegionImplFactory::registerCPPRegion("MyTestNode",
                                       new RegisteredRegionImpl<MyTestNode>());

  Network net;
  auto region1 = net.addRegion("region1", "MyTestNode", "{count: 64}");
  auto region2 = net.addRegion("region2", "TestNode", "");


  // NOTE: The propogationDelay is 2 time periods.
  //       The PropogationDelay queue has two buffers.
  //       initial delayed values are set to all 0's
  net.link("region1", "region2", "", "", "", "", 2);

  net.initialize();

  Input *in1 = region1->getInput("bottomUpIn");
  Input *in2 = region2->getInput("bottomUpIn");
  Output *out1 = region1->getOutput("bottomUpOut");

  // test isInitialized()
  ASSERT_TRUE(in1->isInitialized());
  ASSERT_TRUE(in2->isInitialized());


  // set in2 to all 1's, to detect if net.run fails to update the input.
  {
    Real64 *idata = (Real64 *)in2->getData().getBuffer();
    ASSERT_EQ(in2->getData().getCount(), 64);
    for (UInt i = 0; i < 64; i++)
      idata[i] = 1;
  }

  // set out1 to all 10's
  {
    Real64 *idata = (Real64 *)out1->getData().getBuffer();
    ASSERT_EQ(out1->getData().getCount(), 64);
    for (UInt i = 0; i < 64; i++)
      idata[i] = 10;
  }

  // Check extraction of first delayed value
  {
    // This run should also pick up the 10s and move them
    // into the bottom buffer of the PropogationDelay queue.
    // The top buffer should be 0's
    net.run(1);

    // confirm that the output still contains all 10's
    // In other words, that our hacked up version of TestNode 
    // did not output anything that would clobber the output buffer.
    Real64 *idata = (Real64 *)out1->getData().getBuffer();
    // only test 4 instead of 64 to cut down on number of tests
    for (UInt i = 0; i < 4; i++) {
      ASSERT_EQ(10, idata[i]);
    }
  }

  {
    // confirm that in2 is all zeroes (the buffer at the top of the queue)
    Real64 *idata = (Real64 *)in2->getData().getBuffer();
    // only test 4 instead of 64 to cut down on number of tests
    for (UInt i = 0; i < 4; i++)
      ASSERT_EQ(0, idata[i]);
  }

  // set out1 to all 100's
  {
    Real64 *idata = (Real64 *)out1->getData().getBuffer();
    for (UInt i = 0; i < 64; i++)
      idata[i] = 100;
  }

  // Check extraction of second delayed value
  {
    // This will move the 100's into the end of the queue
    // The 10's will then be at the top of the queue.
    // The 10's are copied to the Input buffer.
    net.run(1);

    // confirm that in2 is all zeroes
    Real64 *idata = (Real64 *)in2->getData().getBuffer();
    // only test 4 instead of 64 to cut down on number of tests
    for (UInt i = 0; i < 4; i++)
      ASSERT_EQ(0, idata[i]);
  }

  // At this point:
  // The current output is all 100's
  // The current input is all 0's
  // We should have two delayed array values in queue: 10's and 100's

  // Serialize the current net
  net.save("TestOutputDir/DelayedLinkSerialization.nta");

  {
    // Output values should still be all 100's
    // they were not modified by the save operation.
    Real64 *idata = (Real64 *)out1->getData().getBuffer();
    // only test 4 instead of 64 to cut down on number of tests
    for (UInt i = 0; i < 4; i++) {
      ASSERT_EQ(100, idata[i]);
    }
  }

  // De-serialize into a new net2
  Network net2("TestOutputDir/DelayedLinkSerialization.nta");

  auto n2region1 = net2.getRegions().getByName("region1");
  auto n2region2 = net2.getRegions().getByName("region2");

  Input *n2in1 = n2region1->getInput("bottomUpIn");
  Input *n2in2 = n2region2->getInput("bottomUpIn");
  Output *n2out1 = n2region1->getOutput("bottomUpOut");

  // Make sure that the buffers in the restored network look exactly like the original.
  ASSERT_TRUE(n2in1->getData() == in1->getData())   << "Deserialized bottomUpIn region1 input buffer does not match";
  ASSERT_TRUE(n2in2->getData() == in2->getData())   << "Deserialized bottomUpIn region2 does not match";
  ASSERT_TRUE(n2out1->getData() == out1->getData()) << "Deserialized bottomUpOut region1 does not match";
  ASSERT_EQ(n2in2->getData().getCount(), 64);

  {
    // Output values in both networks should be all 100's
    Real64 *idata = (Real64 *)out1->getData().getBuffer();
    Real64 *n2idata = (Real64 *)n2out1->getData().getBuffer();
    // only test 4 instead of 64 to cut down on number of tests
    for (UInt i = 0; i < 4; i++) {
      ASSERT_EQ(100, idata[i]);
      ASSERT_EQ(100, n2idata[i]);
    }
  }

  {
    // Input values in both networks should be all 0's
    Real64 *idata = (Real64 *)in2->getData().getBuffer();
    Real64 *n2idata = (Real64 *)n2in2->getData().getBuffer();
    // only test 4 instead of 64 to cut down on number of tests
    for (UInt i = 0; i < 4; i++) {
      ASSERT_EQ(0, idata[i]);
      ASSERT_EQ(0, n2idata[i]);
    }
  }

  // The restore looks good..now lets see if we can continue execution.
  // Check extraction of first "generated" value.
  {
    net.run(1);
    net2.run(1);

    ASSERT_EQ(n2in2->getData().getCount(), 64);
    Real64 *idata = (Real64 *)in2->getData().getBuffer();
    Real64 *n2idata = (Real64 *)n2in2->getData().getBuffer();
    // only test 4 instead of 64 to cut down on number of tests
    for (UInt i = 0; i < 4; i++) {
      ASSERT_EQ(10, idata[i]);
      ASSERT_EQ(10, n2idata[i]);
    }
  }

  // Check extraction of second "generated" value
  // Input values in both networks should be all 100's
  {
    net.run(1);
    net2.run(1);

    // confirm that in2 is now all 100's
    Real64 *idata = (Real64 *)in2->getData().getBuffer();
    Real64 *n2idata = (Real64 *)n2in2->getData().getBuffer();
    // only test 4 instead of 64 to cut down on number of tests
    for (UInt i = 0; i < 4; i++) {
      ASSERT_EQ(100, idata[i]);
      ASSERT_EQ(100, n2idata[i]);
    }
  }

  RegionImplFactory::unregisterCPPRegion("MyTestNode");
  Directory::removeTree("TestOutputDir");
}

/******************************************************************
 * Base class for region implementations in this test module. See also
 * L2TestRegion and L4TestRegion.
 */
class TestRegionBase : public RegionImpl {
public:
  TestRegionBase(const ValueMap &params, Region *region) : RegionImpl(region) {

    outputElementCount_ = 1;
  }

  TestRegionBase(BundleIO &bundle, Region *region) : RegionImpl(region) {}

  virtual ~TestRegionBase() {}

  // Serialize state.
  void serialize(BundleIO &bundle) override {}

  // De-serialize state. Must be called from deserializing constructor
  void deserialize(BundleIO &bundle) override {}

  // Execute a command
  std::string executeCommand(const std::vector<std::string> &args,
                             Int64 index) override {
    return "";
  }

  // Per-node size (in elements) of the given output.
  // For per-region outputs, it is the total element count.
  // This method is called only for outputs whose size is not
  // specified in the nodespec.
  size_t getNodeOutputElementCount(const std::string &outputName) override {
    if (outputName == "out") {
      return outputElementCount_;
    }
    NTA_THROW << "TestRegionBase::getOutputSize -- unknown output "
              << outputName;
  }

  /**
   * Get a parameter from a write buffer.
   * This method is called only by the typed getParameter*
   * methods in the RegionImpl base class
   *
   * Must be implemented by all subclasses.
   *
   * @param index A node index. (-1) indicates a region-level parameter
   *
   */
  void getParameterFromBuffer(const std::string &name, Int64 index,
                              IWriteBuffer &value) override {}

  /**
   * Set a parameter from a read buffer.
   * This method is called only by the RegionImpl base class
   * type-specific setParameter* methods
   * Must be implemented by all subclasses.
   *
   * @param index A node index. (-1) indicates a region-level parameter
   */
  void setParameterFromBuffer(const std::string &name, Int64 index,
                              IReadBuffer &value) override {}

private:
  TestRegionBase();

  // Constructor param specifying per-node output size
  UInt32 outputElementCount_;
};

/*
 * This region's output is computed as: feedForwardIn + lateralIn
 */
class L2TestRegion : public TestRegionBase {
public:
  L2TestRegion(const ValueMap &params, Region *region)
      : TestRegionBase(params, region) {}

  L2TestRegion(BundleIO &bundle, Region *region)
      : TestRegionBase(bundle, region) {}

  virtual ~L2TestRegion() {}

  std::string getNodeType() { return "L2TestRegion"; }

  // Used by RegionImplFactory to create and cache
  // a nodespec. Ownership is transferred to the caller.
  static Spec *createSpec() {
    auto ns = new Spec;

    /* ----- inputs ------- */
    ns->inputs.add("feedForwardIn", InputSpec("Feed-forward input for the node",
                                              NTA_BasicType_UInt64,
                                              0,     // count. wildcard
                                              true,  // required?
                                              false, // isRegionLevel,
                                              false  // isDefaultInput
                                              ));

    ns->inputs.add("lateralIn",
                   InputSpec("Lateral input for the node", NTA_BasicType_UInt64,
                             0,     // count. wildcard
                             true,  // required?
                             false, // isRegionLevel,
                             false  // isDefaultInput
                             ));

    /* ----- outputs ------ */
    ns->outputs.add("out", OutputSpec("Primary output for the node",
                                      NTA_BasicType_UInt64,
                                      3,     // 1st is output; 2nd is the given
                                             // feedForwardIn; 3rd is lateralIn
                                      false, // isRegionLevel
                                      true   // isDefaultOutput
                                      ));

    return ns;
  }

  /**
   * Inputs/Outputs are made available in initialize()
   * It is always called after the constructor (or load from serialized state)
   */
  void initialize() override {
    nodeCount_ = 1;
    out_ = getOutput("out");
    feedForwardIn_ = getInput("feedForwardIn");
    lateralIn_ = getInput("lateralIn");
  }

  // Compute outputs from inputs and internal state
  void compute() override {

    const Array &outputArray = out_->getData();
    NTA_CHECK(outputArray.getCount() == 3);
    NTA_CHECK(outputArray.getType() == NTA_BasicType_UInt64);
    UInt64 *baseOutputBuffer = (UInt64 *)outputArray.getBuffer();

    NTA_CHECK(feedForwardIn_->getData().getCount() > 1);
    UInt64 *ffInput = (UInt64 *)feedForwardIn_->getData().getBuffer();

    NTA_CHECK(lateralIn_->getData().getCount() > 1);
    UInt64 *latInput = (UInt64 *)lateralIn_->getData().getBuffer();


    // Only the first element of baseOutputBuffer represents region output. We
    // keep track of inputs to the region using the rest of the baseOutputBuffer
    // vector. These inputs are used in the tests.
    baseOutputBuffer[0] = ffInput[0] + latInput[0];
    baseOutputBuffer[1] = ffInput[0];
    baseOutputBuffer[2] = latInput[0];

    NTA_DEBUG << getName() << ".compute: inff=[" << ffInput[0] << "] inlat=["
              << latInput[0] <<
      "] out=[" << baseOutputBuffer[0] << ","
              << baseOutputBuffer[1] << "," << baseOutputBuffer[2] << "]";
  }

private:
  L2TestRegion();

  /* ----- cached info from region ----- */
  size_t nodeCount_;

  // Input/output buffers for the whole region
  Input *feedForwardIn_;
  Input *lateralIn_;
  Output *out_;
};

class L4TestRegion : public TestRegionBase {
public:
  /*
   * This region's output is computed as: k + feedbackIn
   */
  L4TestRegion(const ValueMap &params, Region *region)
      : TestRegionBase(params, region), k_(params.getScalarT<UInt64>("k")) {}

  L4TestRegion(BundleIO &bundle, Region *region)
      : TestRegionBase(bundle, region), k_(0) {}

  virtual ~L4TestRegion() {}

  std::string getNodeType() { return "L4TestRegion"; }

  // Used by RegionImplFactory to create and cache
  // a nodespec. Ownership is transferred to the caller.
  static Spec *createSpec() {
    auto ns = new Spec;
    /* ---- parameters ------ */
    ns->parameters.add(
        "k",
        ParameterSpec("Constant k value for output computation", // description
                      NTA_BasicType_UInt64,
                      1,  // elementCount
                      "", // constraints
                      "", // defaultValue
                      ParameterSpec::ReadWriteAccess));

    /* ----- inputs ------- */
    ns->inputs.add("feedbackIn", InputSpec("Feedback input for the node",
                                           NTA_BasicType_UInt64,
                                           0,     // count. omit?
                                           true,  // required?
                                           false, // isRegionLevel,
                                           false  // isDefaultInput
                                           ));

    /* ----- outputs ------ */
    ns->outputs.add(
        "out",
        OutputSpec(
            "Primary output for the node", NTA_BasicType_UInt64,
            2, // 2 elements: 1st is output; 2nd is the given feedbackIn value
            false, // isRegionLevel
            true   // isDefaultOutput
            ));

    return ns;
  }

  /**
   * Inputs/Outputs are made available in initialize()
   * It is always called after the constructor (or load from serialized state)
   */
  void initialize() override {
    out_ = getOutput("out");
    feedbackIn_ = getInput("feedbackIn");
  }

  // Compute outputs from inputs and internal state
  void compute() override {
    const Array &outputArray = out_->getData();
    NTA_CHECK(outputArray.getCount() == 2);
    NTA_CHECK(outputArray.getType() == NTA_BasicType_UInt64);
    UInt64 *baseOutputBuffer = (UInt64 *)outputArray.getBuffer();

    NTA_CHECK(feedbackIn_->getData().getCount() >= 1);
    UInt64 *nodeInput = (UInt64 *)feedbackIn_->getData().getBuffer();

    //NTA_DEBUG << getName() << ".compute: fbInput size=" << nodeInput.size()
    //          << "; inputValue=" << nodeInput[0];

    // Only the first element of baseOutputBuffer represents region output. We
    // keep track of inputs to the region using the rest of the baseOutputBuffer
    // vector. These inputs are used in the tests.
    baseOutputBuffer[0] = k_ + nodeInput[0];
    baseOutputBuffer[1] = nodeInput[0];

    NTA_DEBUG << getName() << ".compute: in=[" << nodeInput[0] << "," << nodeInput[1] << "]  out=[" << baseOutputBuffer[0] << ","
              << baseOutputBuffer[1] << "]";
  }

private:
  L4TestRegion();

  const UInt64 k_;

  /* ----- cached info from region ----- */

  // Input/output buffers for the whole region
  Input *feedbackIn_;
  Output *out_;
};

TEST(LinkTest, L2L4WithDelayedLinksAndPhases) {
  // This test simulates a network with L2 and L4, structured as follows:
  // o R1/R2 ("L4") are in phase 1; R3/R4 ("L2") are in phase 2;
  // o feed-forward links with delay=0 from R1/R2 to R3/R4, respectively;
  // o lateral links with delay=1 between R3 and R4;
  // o feedback links with delay=1 from R3/R4 to R1/R2, respectively
  //
  // Order of data movement:                        values during propogation (in link)
  //                                        Iteration1         Iteration2         Iteration3
  // phase1:                               out      in        out       in         out     in
  //   R1.out -> R3.feedForwardIn         [1,0] -> [1,0]    [2,1]   -> [2,1]    [8,7]  ->[8,7]
  //   R2.out -> R4.feedForwardIn         [5,0] -> [5,0]    [10,5]  -> [10,5]   [16,11] ->[16,11]
  // phase2:
  //   R3.out -> R1.feedbackIn  Delay 1   [1,1,0]  [0,0,0]  [7,2,5]    [1,1,0]  [19,8,11]  [7,2,5]
  //             delayQue                        ->[1,1,0]           ->[7,2,5]           ->[19,8,11]
  //   R3.out -> R4.LateralIn   Delay 1   [1,1,0]->[0,0,0]  [7,2,5]  ->[1,1,0]  [19,8,11]->[7,2,5]
  //             delayQue                        ->[1,1,0]           ->[7,2,5]           ->[19,8,11]
  //   R4.out -> R2.feedbackIn  Delay 1   [5,5,0]->[0,0,0]  [11,10,1]->[5,5,0]  [23,16,7]->[11,10,1]
  //             delayQue                        ->[5,5,0]           ->[11,10,1]         ->[23,16,7]
  //   R4.out -> R2.LateralIn   Delay 1   [5,5,0]->[0,0,0]  [11,10,1]->[5,5,0]  [23,16,7]->[11,10,1]
  //             delayQue                        ->[5,5,0]           ->[11,10,1]         ->[23,16,7]
  //
  //                                                values at execution (in region)
  //                                        Iteration1         Iteration2         Iteration3
  // phase1:                               in      out        in       out         in     out
  //   R1.feedbackIn -> R1.out            [0,0] -> [1,0]    [1,1]   -> [2,1]     [7,2]    ->[8,7]
  //   R2.feedbackIn -> R2.out            [0,0] -> [5,0]    [5,5]   -> [10,5]    [11,10]  ->[16,11]
  // phase2:
  //   R3.feedforwardIn ->                [1,0]             [2,1]                [8,7]
  //   R3.LateralIn     -> R3.out         [0,0,0]->[1,1,0]  [5,5,0]  ->[7,2,5]   [11,10,1]->[19,8,11] 
  //   R4.feedforwardIn ->                [5,0]             [10,5]               [16,11]
  //   R4.LateralIn     -> R4.out         [0,0,0]->[5,5,0]  [1,1,0]  ->[11,10,1] [7,2,5]  ->[23,16,7]
  //   

  Network net;

  RegionImplFactory::registerCPPRegion(
      "L4TestRegion", new RegisteredRegionImpl<L4TestRegion>());
  auto r1 = net.addRegion("R1", "L4TestRegion", "{\"k\": 1}");
  auto r2 = net.addRegion("R2", "L4TestRegion", "{\"k\": 5}");
  RegionImplFactory::unregisterCPPRegion("L4TestRegion");

  RegionImplFactory::registerCPPRegion(
      "L2TestRegion", new RegisteredRegionImpl<L2TestRegion>());
  auto r3 = net.addRegion("R3", "L2TestRegion", "");
  auto r4 = net.addRegion("R4", "L2TestRegion", "");
  RegionImplFactory::unregisterCPPRegion("L2TestRegion");


  /* Set region phases */

  std::set<UInt32> phases;
  phases.insert(1);
  net.setPhases("R1", phases);
  net.setPhases("R2", phases);

  phases.clear();
  phases.insert(2);
  net.setPhases("R3", phases);
  net.setPhases("R4", phases);

  /* Link up the network */

  // R1 output
  net.link("R1",            // srcName
           "R3",            // destName
           "",              // linkType
           "",              // linkParams
           "out",           // srcOutput
           "feedForwardIn", // destInput
           0                // propagationDelay
  );

  // R2 output
  net.link("R2",            // srcName
           "R4",            // destName
           "",              // linkType
           "",              // linkParams
           "out",           // srcOutput
           "feedForwardIn", // destInput
           0                // propagationDelay
  );

  // R3 outputs
  net.link("R3",          // srcName
           "R1",          // destName
           "UniformLink", // linkType
           "",            // linkParams
           "out",         // srcOutput
           "feedbackIn",  // destInput
           1              // propagationDelay
  );

  net.link("R3",          // srcName
           "R4",          // destName
           "UniformLink", // linkType
           "",            // linkParams
           "out",         // srcOutput
           "lateralIn",   // destInput
           1              // propagationDelay
  );

  // R4 outputs
  net.link("R4",          // srcName
           "R2",          // destName
           "UniformLink", // linkType
           "",            // linkParams
           "out",         // srcOutput
           "feedbackIn",  // destInput
           1              // propagationDelay
  );

  net.link("R4",          // srcName
           "R3",          // destName
           "UniformLink", // linkType
           "",            // linkParams
           "out",         // srcOutput
           "lateralIn",   // destInput
           1              // propagationDelay
  );

  // Initialize the network
  net.initialize();

  // Note: During propagation the buffer address may change due to ZeroCopy
  //UInt64 *r1OutBuf = (UInt64 *)(r1->getOutput("out")->getData().getBuffer());
  //UInt64 *r2OutBuf = (UInt64 *)(r2->getOutput("out")->getData().getBuffer());
  //UInt64 *r3OutBuf = (UInt64 *)(r3->getOutput("out")->getData().getBuffer());
  //UInt64 *r4OutBuf = (UInt64 *)(r4->getOutput("out")->getData().getBuffer());

  Array& a1 = r1->getOutput("out")->getData();
  Array& a2 = r2->getOutput("out")->getData();
  Array& a3 = r3->getOutput("out")->getData();
  Array& a4 = r4->getOutput("out")->getData();
  UInt64 *r1OutBuf;
  UInt64 *r2OutBuf;
  UInt64 *r3OutBuf;
  UInt64 *r4OutBuf;


  /* ITERATION #1 */
  net.run(1);

  r1OutBuf = (UInt64 *)(a1.getBuffer());
  r2OutBuf = (UInt64 *)(a2.getBuffer());
  r3OutBuf = (UInt64 *)(a3.getBuffer());
  r4OutBuf = (UInt64 *)(a4.getBuffer());

  // Validate R1
  ASSERT_EQ(1u, r1OutBuf[0]); // out (1 + feedbackIn)
  ASSERT_EQ(0u, r1OutBuf[1]); // feedbackIn from R3; delay=1

  // Validate R2
  ASSERT_EQ(5u, r2OutBuf[0]); // out (5 + feedbackIn)
  ASSERT_EQ(0u, r2OutBuf[1]); // feedbackIn from R4; delay=1

  // Validate R3
  ASSERT_EQ(1u, r3OutBuf[0]); // out (feedForwardIn + lateralIn)
  ASSERT_EQ(1u, r3OutBuf[1]); // feedForwardIn from R1; delay=0
  ASSERT_EQ(0u, r3OutBuf[2]); // lateralIn from R4; delay=1

  // Validate R4
  ASSERT_EQ(5u, r4OutBuf[0]); // out (feedForwardIn + lateralIn)
  ASSERT_EQ(5u, r4OutBuf[1]); // feedForwardIn from R2; delay=0
  ASSERT_EQ(0u, r4OutBuf[2]); // lateralIn from R3; delay=1

  /* ITERATION #2 */
  net.run(1);
  r1OutBuf = (UInt64 *)(a1.getBuffer());
  r2OutBuf = (UInt64 *)(a2.getBuffer());
  r3OutBuf = (UInt64 *)(a3.getBuffer());
  r4OutBuf = (UInt64 *)(a4.getBuffer());

  // Validate R1
  ASSERT_EQ(2u, r1OutBuf[0]); // out (1 + feedbackIn)
  ASSERT_EQ(1u, r1OutBuf[1]); // feedbackIn from R3; delay=1

  // Validate R2
  ASSERT_EQ(10u, r2OutBuf[0]); // out (5 + feedbackIn)
  ASSERT_EQ(5u,  r2OutBuf[1]);  // feedbackIn from R4; delay=1

  // Validate R3
  ASSERT_EQ(7u, r3OutBuf[0]); // out (feedForwardIn + lateralIn)
  ASSERT_EQ(2u, r3OutBuf[1]); // feedForwardIn from R1; delay=0
  ASSERT_EQ(5u, r3OutBuf[2]); // lateralIn from R4; delay=1

  // Validate R4
  ASSERT_EQ(11u, r4OutBuf[0]); // out (feedForwardIn + lateralIn)
  ASSERT_EQ(10u, r4OutBuf[1]); // feedForwardIn from R2; delay=0
  ASSERT_EQ(1u,  r4OutBuf[2]);  // lateralIn from R3; delay=1

  /* ITERATION #3 */
  net.run(1);
  r1OutBuf = (UInt64 *)(a1.getBuffer());
  r2OutBuf = (UInt64 *)(a2.getBuffer());
  r3OutBuf = (UInt64 *)(a3.getBuffer());
  r4OutBuf = (UInt64 *)(a4.getBuffer());

  // Validate R1
  ASSERT_EQ(8u, r1OutBuf[0]); // out (1 + feedbackIn)
  ASSERT_EQ(7u, r1OutBuf[1]); // feedbackIn from R3; delay=1

  // Validate R2
  ASSERT_EQ(16u, r2OutBuf[0]); // out (5 + feedbackIn)
  ASSERT_EQ(11u, r2OutBuf[1]); // feedbackIn from R4; delay=1

  // Validate R3
  ASSERT_EQ(19u, r3OutBuf[0]); // out (feedForwardIn + lateralIn)
  ASSERT_EQ(8u,  r3OutBuf[1]);  // feedForwardIn from R1; delay=0
  ASSERT_EQ(11u, r3OutBuf[2]); // lateralIn from R4; delay=1

  // Validate R4
  ASSERT_EQ(23u, r4OutBuf[0]); // out (feedForwardIn + lateralIn)
  ASSERT_EQ(16u, r4OutBuf[1]); // feedForwardIn from R2; delay=0
  ASSERT_EQ(7u,  r4OutBuf[2]);  // lateralIn from R3; delay=1
}

TEST(LinkTest, L2L4With1ColDelayedLinksAndPhase1OnOffOn) {
  // Validates processing of incoming delayed and outgoing non-delayed link in
  // the context of a region within a suppressed phase.
  //
  // This test simulates a network with a single L2/L4 column, structured as
  // follows:
  // o R1 ("L4") is in phase 1; R3 ("L2") is in phase 2;
  // o feed-forward link with delay=0 from R1 to R3
  // o lateral link with delay=1 looping back from R3 to itself
  // o feedback link with delay=1 from R3 to R1
  //
  // Running the network:
  // o Run 1 time step with both phases enabled
  // o Disable phase 1 and run two time steps
  // o Enable phase 1 and run two time steps

  Network net;

  RegionImplFactory::registerCPPRegion(
      "L4TestRegion", new RegisteredRegionImpl<L4TestRegion>());
  auto r1 = net.addRegion("R1", "L4TestRegion", "{\"k\": 1}");
  RegionImplFactory::unregisterCPPRegion("L4TestRegion");

  RegionImplFactory::registerCPPRegion(
      "L2TestRegion", new RegisteredRegionImpl<L2TestRegion>());
  auto r3 = net.addRegion("R3", "L2TestRegion", "");
  RegionImplFactory::unregisterCPPRegion("L2TestRegion");


  /* Set region phases */

  std::set<UInt32> phases;
  phases.insert(1);
  net.setPhases("R1", phases);

  phases.clear();
  phases.insert(2);
  net.setPhases("R3", phases);

  /* Link up the network */

  // R1 output
  net.link("R1",            // srcName
           "R3",            // destName
           "UniformLink",   // linkType
           "",              // linkParams
           "out",           // srcOutput
           "feedForwardIn", // destInput
           0                // propagationDelay
  );

  // R3 outputs
  net.link("R3",          // srcName
           "R1",          // destName
           "UniformLink", // linkType
           "",            // linkParams
           "out",         // srcOutput
           "feedbackIn",  // destInput
           1              // propagationDelay
  );

  net.link("R3",          // srcName
           "R3",          // destName
           "UniformLink", // linkType
           "",            // linkParams
           "out",         // srcOutput
           "lateralIn",   // destInput
           1              // propagationDelay
  );

  // Initialize the network
  net.initialize();

  UInt64 *r1OutBuf = (UInt64 *)(r1->getOutput("out")->getData().getBuffer());
  UInt64 *r3OutBuf = (UInt64 *)(r3->getOutput("out")->getData().getBuffer());

  /* ITERATION #1 with all phases enabled */
  net.run(1);

  // Validate R1
  ASSERT_EQ(0u, r1OutBuf[1]); // feedbackIn from R3; delay=1
  ASSERT_EQ(1u, r1OutBuf[0]); // out (1 + feedbackIn)

  // Validate R3
  ASSERT_EQ(1u, r3OutBuf[1]); // feedForwardIn from R1; delay=0
  ASSERT_EQ(0u, r3OutBuf[2]); // lateralIn loopback from R3; delay=1
  ASSERT_EQ(1u, r3OutBuf[0]); // out (feedForwardIn + lateralIn)

  /* Disable Phase 1, containing R1 */
  net.setMinEnabledPhase(2);

  /* ITERATION #2 with Phase 1 disabled */
  net.run(1);

  // Validate R1 (it's in a disabled phase, so should be stuck at prior values)
  ASSERT_EQ(0u, r1OutBuf[1]); // feedbackIn
  ASSERT_EQ(1u, r1OutBuf[0]); // out

  // Validate R3
  ASSERT_EQ(1u, r3OutBuf[1]); // feedForwardIn from R1; delay=0
  ASSERT_EQ(1u, r3OutBuf[2]); // lateralIn loopback from R3; delay=1
  ASSERT_EQ(2u, r3OutBuf[0]); // out (feedForwardIn + lateralIn)

  /* ITERATION #3 with Phase 1 disabled */
  net.run(1);

  // Validate R1 (it's in a disabled phase, so should be stuck at prior values)
  ASSERT_EQ(0u, r1OutBuf[1]); // feedbackIn
  ASSERT_EQ(1u, r1OutBuf[0]); // out

  // Validate R3
  ASSERT_EQ(1u, r3OutBuf[1]); // feedForwardIn from R1; delay=0
  ASSERT_EQ(2u, r3OutBuf[2]); // lateralIn loopback from R3; delay=1
  ASSERT_EQ(3u, r3OutBuf[0]); // out (feedForwardIn + lateralIn)

  /* Enable Phase 1, containing R1 */
  net.setMinEnabledPhase(1);

  /* ITERATION #4 with all phases enabled */
  net.run(1);

  // Validate R1
  ASSERT_EQ(3u, r1OutBuf[1]); // feedbackIn from R3; delay=1
  ASSERT_EQ(4u, r1OutBuf[0]); // out (1 + feedbackIn)

  // Validate R3
  ASSERT_EQ(4u, r3OutBuf[1]); // feedForwardIn from R1; delay=0
  ASSERT_EQ(3u, r3OutBuf[2]); // lateralIn loopback from R3; delay=1
  ASSERT_EQ(7u, r3OutBuf[0]); // out (feedForwardIn + lateralIn)

  /* ITERATION #5 with all phases enabled */
  net.run(1);

  // Validate R1
  ASSERT_EQ(7u, r1OutBuf[1]); // feedbackIn from R3; delay=1
  ASSERT_EQ(8u, r1OutBuf[0]); // out (1 + feedbackIn)

  // Validate R3
  ASSERT_EQ(8u, r3OutBuf[1]);  // feedForwardIn from R1; delay=0
  ASSERT_EQ(7u, r3OutBuf[2]);  // lateralIn loopback from R3; delay=1
  ASSERT_EQ(15u, r3OutBuf[0]); // out (feedForwardIn + lateralIn)
}

TEST(LinkTest, SingleL4RegionWithDelayedLoopbackInAndPhaseOnOffOn) {
  // Validates processing of outgoing/incoming delayed link in the context of a
  // region within a disabled phase.
  //
  // This test simulates a network with a single L4 region, structured as
  // follows:
  // o R1 ("L4") is in phase 1
  // o Loopback link with delay=1 from R1 to itself
  //
  // Running the network:
  // o Run 1 time step with phase 1 enabled
  // o Disable phase 1 and run two time steps
  // o Enable phase 1 and run two time steps

  Network net;

  RegionImplFactory::registerCPPRegion(
      "L4TestRegion", new RegisteredRegionImpl<L4TestRegion>());
  auto r1 = net.addRegion("R1", "L4TestRegion", "{\"k\": 1}");
  RegionImplFactory::unregisterCPPRegion("L4TestRegion");


  /* Set region phases */

  std::set<UInt32> phases;
  phases.insert(1);
  net.setPhases("R1", phases);

  /* Link up the network */

  // R1 output (loopback)
  net.link("R1",          // srcName
           "R1",          // destName
           "UniformLink", // linkType
           "",            // linkParams
           "out",         // srcOutput
           "feedbackIn",  // destInput
           1              // propagationDelay
  );

  // Initialize the network
  net.initialize();

  UInt64 *r1OutBuf = (UInt64 *)(r1->getOutput("out")->getData().getBuffer());

  /* ITERATION #1 with phase 1 enabled */
  net.run(1);

  // Validate R1
  ASSERT_EQ(0u, r1OutBuf[1]); // feedbackIn from R3; delay=1
  ASSERT_EQ(1u, r1OutBuf[0]); // out (1 + feedbackIn)

  /* Disable Phase 1, containing R1 */
  net.setMaxEnabledPhase(0);

  /* ITERATION #2 with Phase 1 disabled */
  net.run(1);

  // Validate R1 (it's in a disabled phase, so should be stuck at prior values)
  ASSERT_EQ(0u, r1OutBuf[1]); // feedbackIn
  ASSERT_EQ(1u, r1OutBuf[0]); // out

  /* ITERATION #3 with Phase 1 disabled */
  net.run(1);

  // Validate R1 (it's in a disabled phase, so should be stuck at prior values)
  ASSERT_EQ(0u, r1OutBuf[1]); // feedbackIn
  ASSERT_EQ(1u, r1OutBuf[0]); // out

  /* Enable Phase 1, containing R1 */
  net.setMaxEnabledPhase(1);

  /* ITERATION #4 with phase 1 enabled */
  net.run(1);

  // Validate R1
  ASSERT_EQ(1u, r1OutBuf[1]); // feedbackIn from R3; delay=1
  ASSERT_EQ(2u, r1OutBuf[0]); // out (1 + feedbackIn)

  /* ITERATION #5 with phase 1 enabled */
  net.run(1);

  // Validate R1
  ASSERT_EQ(2u, r1OutBuf[1]); // feedbackIn from R3; delay=1
  ASSERT_EQ(3u, r1OutBuf[0]); // out (1 + feedbackIn)
}
