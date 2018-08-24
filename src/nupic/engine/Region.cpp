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

/** @file
Implementation of the Region class

Methods related to parameters are in Region_parameters.cpp
Methods related to inputs and outputs are in Region_io.cpp

*/

#include <iostream>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>

#include <nupic/engine/Input.hpp>
#include <nupic/engine/Link.hpp>
#include <nupic/engine/Output.hpp>
#include <nupic/engine/Region.hpp>
#include <nupic/engine/RegionImpl.hpp>
#include <nupic/engine/RegionImplFactory.hpp>
#include <nupic/engine/Spec.hpp>
#include <nupic/os/Timer.hpp>
#include <nupic/utils/Log.hpp>
#include <nupic/ntypes/BundleIO.hpp>

namespace nupic {

class RegisteredRegionImpl;

// Create region from parameter spec
Region::Region(std::string name, const std::string &nodeType,
               const std::string &nodeParams, Network *network)
    : name_(std::move(name)), type_(nodeType), initialized_(false),
      network_(network), profilingEnabled_(false)
{
  // Set region info before creating the RegionImpl so that the
  // Impl has access to the region info in its constructor.
  // Get the spec from the factory. It is cached by RegisteredRegionImpl.
  RegionImplFactory &factory = RegionImplFactory::getInstance();
  spec_ = factory.getSpec(nodeType);


  // This returns a new instance of the nodeType.  This class must free.
  impl_ = factory.createRegionImpl(nodeType, nodeParams, this);
  createInputsAndOutputs_();
}

Region::Region(Network *net) {
      network_ = net;
      spec_ = nullptr;
      impl_ = nullptr;
      initialized_ = false;
      profilingEnabled_ = false;
    } // for deserialization of region.



Network *Region::getNetwork() { return network_; }

void Region::createInputsAndOutputs_() {

  // Create all the outputs for this node type. By default outputs are zero size
  for (size_t i = 0; i < spec_->outputs.getCount(); ++i) {
    const std::pair<std::string, OutputSpec> &p = spec_->outputs.getByIndex(i);
    std::string outputName = p.first;
    const OutputSpec &os = p.second;
    auto output = new Output(*this, os.dataType);
    outputs_[outputName] = output;
    // keep track of name in the output also -- see note in Region.hpp
    output->setName(outputName);
  }

  // Create all the inputs for this node type.
  for (size_t i = 0; i < spec_->inputs.getCount(); ++i) {
    const std::pair<std::string, InputSpec> &p = spec_->inputs.getByIndex(i);
    std::string inputName = p.first;
    const InputSpec &is = p.second;

    auto input = new Input(*this, is.dataType);
    inputs_[inputName] = input;
    // keep track of name in the input also -- see note in Region.hpp
    input->setName(inputName);
  }
}

bool Region::hasOutgoingLinks() const {
  for (const auto &elem : outputs_) {
    if (elem.second->hasOutgoingLinks()) {
      return true;
    }
  }
  return false;
}

Region::~Region() {
  if (initialized_)
    uninitialize();

  // If there are any links connected to our outputs, this should fail.
  // We catch this error in the Network class and give the
  // user a good error message (regions may be removed either in
  // Network::removeRegion or Network::~Network())
  for (auto &elem : outputs_) {
    delete elem.second;
    elem.second = nullptr;
  }
  outputs_.clear();

  for (auto &elem : inputs_) {
    delete elem.second; // This is an Input object. Its destructor deletes the links.
    elem.second = nullptr;
  }
  inputs_.clear();

  if (impl_)
    delete impl_;

}

void Region::initialize() {

  if (initialized_)
    return;

  impl_->initialize();
  initialized_ = true;
}


const Spec *Region::getSpecFromType(const std::string &nodeType) {
  RegionImplFactory &factory = RegionImplFactory::getInstance();
  return factory.getSpec(nodeType);
}

void Region::registerCPPRegion(const std::string name,
                               RegisteredRegionImpl *wrapper) {
  RegionImplFactory::registerCPPRegion(name, wrapper);
}

void Region::unregisterCPPRegion(const std::string name) {
  RegionImplFactory::unregisterCPPRegion(name);
}

void Region::enable() {
  NTA_THROW << "Region::enable not implemented (region name: " << getName()
            << ")";
}

void Region::disable() {
  NTA_THROW << "Region::disable not implemented (region name: " << getName()
            << ")";
}

std::string Region::executeCommand(const std::vector<std::string> &args) {
  std::string retVal;
  if (args.size() < 1) {
    NTA_THROW << "Invalid empty command specified";
  }

  if (profilingEnabled_)
    executeTimer_.start();

  retVal = impl_->executeCommand(args, (UInt64)(-1));

  if (profilingEnabled_)
    executeTimer_.stop();

  return retVal;
}

void Region::compute() {
  if (!initialized_)
    NTA_THROW << "Region " << getName()
              << " unable to compute because not initialized";

  if (profilingEnabled_)
    computeTimer_.start();

  impl_->compute();

  if (profilingEnabled_)
    computeTimer_.stop();

  return;
}


size_t Region::getNodeOutputElementCount(const std::string &name) {
  // Use output count if specified in nodespec, otherwise
  // ask the region Impl what it expects to produce.
  NTA_CHECK(spec_->outputs.contains(name));
  size_t count = spec_->outputs.getByName(name).count;
  if (count == 0) {
    try {
      count = impl_->getNodeOutputElementCount(name);
    } catch (Exception &e) {
      NTA_THROW << "Internal error -- the size for the output " << name
                << "is unknown. : " << e.what();
    }
  }

  return count;
}

void Region::initOutputs() {
  // Called by Network during initialization.
  // Some outputs are optional. These outputs will have 0 elementCount in the
  // node spec and also return 0 from impl->getNodeOutputElementCount(). These
  // outputs still appear in the output map, but with an array size of 0. All
  // other outputs we initialize to size determined by spec or by impl.

  for (auto &elem : outputs_) {
    const std::string &name = elem.first;

    size_t count = 0;
    try {
      count = getNodeOutputElementCount(name);
    } catch (nupic::Exception &e) {
      NTA_THROW << "Internal error -- unable to get size of output " << name
                << " : " << e.what();
    }
    elem.second->initialize(count);
  }
}

void Region::initInputs() const {
  auto i = inputs_.begin();
  for (; i != inputs_.end(); i++) {
    i->second->initialize();
  }
}



void Region::removeAllIncomingLinks() {
  InputMap::const_iterator i = inputs_.begin();
  for (; i != inputs_.end(); i++) {
    std::vector<Link_Ptr_t> links = i->second->getLinks();
    for (auto &links_link : links) {
      i->second->removeLink(links_link);
    }
  }
}

void Region::uninitialize() { initialized_ = false; }

void Region::setPhases(std::set<UInt32> &phases) { phases_ = phases; }

std::set<UInt32> &Region::getPhases() { return phases_; }


void Region::save(std::ostream &f) const {
      f << "{\n";
      f << "name: " << name_ << "\n";
      f << "nodeType: " << type_ << "\n";
    /**** remove
	  f << "dimensions: [ " << dims_.size() << "\n";
	  for (Size d : dims_) {
	  	f << d << " ";
	  }
	  f << "]\n";
	  ***/

      f << "phases: [ " << phases_.size() << "\n";
      for (const auto &phases_phase : phases_) {
        f << phases_phase << " ";
      }
      f << "]\n";
      f << "RegionImpl:\n";
      // Now serialize the RegionImpl plugin.
      BundleIO bundle(&f);
      impl_->serialize(bundle);

      f << "}\n";
}

void Region::load(std::istream &f) {
  char bigbuffer[5000];
  std::string tag;
  Size count;

  // Each region is a map -- extract the 4 values in the map
  f >> tag;
  NTA_CHECK(tag == "{") << "bad region entry (not a map)";

  // 1. name
  f >> tag;
  NTA_CHECK(tag == "name:");
  f.ignore(1);
  f.getline(bigbuffer, sizeof(bigbuffer));
  name_ = bigbuffer;

  // 2. nodeType
  f >> tag;
  NTA_CHECK(tag == "nodeType:");
  f.ignore(1);
  f.getline(bigbuffer, sizeof(bigbuffer));
  type_ = bigbuffer;
  /**** remove
  // 3. dimensions
  f >> tag;
  NTA_CHECK(tag == "dimensions");
  f >> tag;
  NTA_CHECK(tag == "[") << "Expecting a sequence.";
  f >> count;
  for (size_t i = 0; i < count; i++)
  {
  Size val;
  f >> val;
    dimensions_.push_back(val);
  }
  f >> tag;
  NTA_CHECK(tag == "]") << "Expecting end of a sequence.";
  ***/

  // 3. phases
  f >> tag;
  NTA_CHECK(tag == "phases:");
  f >> tag;
  NTA_CHECK(tag == "[") << "Expecting a sequence.";
  f >> count;
  phases_.clear();
  for (Size i = 0; i < count; i++)
  {
    UInt32 val;
    f >> val;
    phases_.insert(val);
  }
  f >> tag;
  NTA_CHECK(tag == "]") << "Expected end of sequence of phases.";

  // 4. impl
  f >> tag;
  NTA_CHECK(tag == "RegionImpl:") << "Expected beginning of RegionImpl.";
  f.ignore(1);

  RegionImplFactory &factory = RegionImplFactory::getInstance();
  spec_ = factory.getSpec(type_);
  createInputsAndOutputs_();

  BundleIO bundle(&f);
  impl_ = factory.deserializeRegionImpl(type_, bundle, this);

  f >> tag;
  NTA_CHECK(tag == "}") << "Expected end of region";
}



void Region::enableProfiling() { profilingEnabled_ = true; }

void Region::disableProfiling() { profilingEnabled_ = false; }

void Region::resetProfiling() {
  computeTimer_.reset();
  executeTimer_.reset();
}

const Timer &Region::getComputeTimer() const { return computeTimer_; }

const Timer &Region::getExecuteTimer() const { return executeTimer_; }

} // namespace nupic
