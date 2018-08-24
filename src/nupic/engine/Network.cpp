/* ---------------------------------------------------------------------
 * Numenta Platform for Intelligent Computing (NuPIC)
 * Copyright (C) 2013-2017, Numenta, Inc.  Unless you have an agreement
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
Implementation of the Network class
*/

#include <iostream>
#include <limits>
#include <sstream>
#include <stdlib.h>
#include <stdexcept>

#include <nupic/types/ptr_types.hpp>

#include <nupic/engine/Network.hpp>
#include <nupic/engine/Region.hpp>
#include <nupic/engine/Spec.hpp>
#include <nupic/engine/Link.hpp>
#include <nupic/engine/Input.hpp>
#include <nupic/engine/Output.hpp>
#include <nupic/ntypes/Array.hpp>
#include <nupic/utils/Log.hpp>
#include <nupic/utils/StringUtils.hpp>
#include <nupic/engine/NuPIC.hpp>
#include <nupic/os/Path.hpp>
#include <nupic/os/Directory.hpp>
#include <nupic/ntypes/BundleIO.hpp>


namespace nupic
{

class RegisteredRegionImpl;

Network::Network()
{
  commonInit();
  NuPIC::registerNetwork(this);
}



void Network::commonInit()
{
  initialized_ = false;
  iteration_ = 0;
  minEnabledPhase_ = 0;
  maxEnabledPhase_ = 0;
  // automatic initialization of NuPIC, so users don't
  // have to call NuPIC::initialize
  NuPIC::init();
}

Network::~Network()
{
  NuPIC::unregisterNetwork(this);
  /**
   * Teardown choreography:
   * - unintialize all regions because otherwise we won't be able to disconnect
   * - remove all links, because we can't delete connected regions
   * - delete the regions themselves.
   */

  // 1. uninitialize
  for (size_t i = 0; i < regions_.getCount(); i++)
  {
      Region_Ptr_t r = regions_.getByIndex(i).second;
    r->uninitialize();
  }

  // 2. remove all links
  for (size_t i = 0; i < regions_.getCount(); i++)
  {
      Region_Ptr_t r = regions_.getByIndex(i).second;
    r->removeAllIncomingLinks();
  }

  // 3. delete the regions
  // The Collection container contains a shared_ptr to the region
  // so when the container is deleted, the regions are deleted.
  //for (size_t i = 0; i < regions_.getCount(); i++)
  //{
  //  const std::pair<std::string, Region_Ptr_t>& item =  regions_.getByIndex(i);
  //  item.second.reset();
  //}
}


Region_Ptr_t Network::addRegion(const std::string& name,
                           const std::string& nodeType,
                           const std::string& nodeParams) // yaml formatted parameters as overrides to Spec parameters.
{
  if (regions_.contains(name))
    NTA_THROW << "Region with name '" << name << "' already exists in network";

  auto r = std::make_shared<Region>(name, nodeType, nodeParams, this);
  regions_.add(name, r);
  initialized_ = false;

  setDefaultPhase_(r);
  return r;
}

void Network::setDefaultPhase_(Region_Ptr_t region)
{
  UInt32 newphase = (UInt32)phaseInfo_.size();
  std::set<UInt32> phases;
  phases.insert(newphase);
  setPhases_(region, phases);
}



void
Network::setPhases_(Region_Ptr_t r, std::set<UInt32>& phases)
{
  if (phases.empty())
    NTA_THROW << "Attempt to set empty phase list for region " << r->getName();

  UInt32 maxNewPhase = *(phases.rbegin());
  UInt32 nextPhase = (UInt32)phaseInfo_.size();
  if (maxNewPhase >= nextPhase)
  {
    // It is very unlikely that someone would add a region
    // with a phase much greater than the phase of any other
    // region. This sanity check catches such problems,
    // though it should arguably be legal to set any phase.
    if (maxNewPhase - nextPhase > 3)
      NTA_THROW << "Attempt to set phase of " << maxNewPhase
                << " when expected next phase is " << nextPhase
                << " -- this is probably an error.";

    phaseInfo_.resize(maxNewPhase+1);
  }
  for (UInt i = 0; i < phaseInfo_.size(); i++)
  {
    bool insertPhase = false;
    if (phases.find(i) != phases.end())
      insertPhase = true;

    // remove previous settings for this region
    std::set<Region_Ptr_t>::iterator item;
    item = phaseInfo_[i].find(r);
    if (item != phaseInfo_[i].end() && !insertPhase)
    {
      phaseInfo_[i].erase(item);
    } else if (insertPhase)
    {
      phaseInfo_[i].insert(r);
    }
  }

  // keep track (redundantly) of phases inside the Region also, for serialization
  r->setPhases(phases);

  resetEnabledPhases_();

}

void Network::resetEnabledPhases_()
{
  // min/max enabled phases based on what is in the network
  minEnabledPhase_ = getMinPhase();
  maxEnabledPhase_ = getMaxPhase();
}


void Network::setPhases(const std::string& name, std::set<UInt32>& phases)
{
  if (! regions_.contains(name))
    NTA_THROW << "setPhases -- no region exists with name '" << name << "'";

  Region_Ptr_t r = regions_.getByName(name);
  setPhases_(r, phases);
}

std::set<UInt32> Network::getPhases(const std::string& name) const
{
  if (! regions_.contains(name))
    NTA_THROW << "setPhases -- no region exists with name '" << name << "'";

  Region_Ptr_t r = regions_.getByName(name);

  std::set<UInt32> phases;
  // construct the set of phases enabled for this region
  for (UInt32 i = 0; i < phaseInfo_.size(); i++)
  {
    if (phaseInfo_[i].find(r) != phaseInfo_[i].end())
    {
      phases.insert(i);
    }
  }
  return phases;
}


void Network::removeRegion(const std::string& name)
{
  if (! regions_.contains(name))
    NTA_THROW << "removeRegion: no region named '" << name << "'";

  Region_Ptr_t r = regions_.getByName(name);
  if (r->hasOutgoingLinks())
    NTA_THROW << "Unable to remove region '" << name << "' because it has one or more outgoing links";

  // Network does not have to be uninitialized -- removing a region
  // has no effect on the network as long as it has no outgoing links,
  // which we have already checked.
  // initialized_ = false;

  // Must uninitialize the region prior to removing incoming links
  r->uninitialize();
  r->removeAllIncomingLinks();

  regions_.remove(name);

  auto phase = phaseInfo_.begin();
  for (; phase != phaseInfo_.end(); phase++)
  {
    auto toremove = phase->find(r);
    if (toremove != phase->end())
      phase->erase(toremove);
  }

  // Trim phaseinfo as we may have no more regions at the highest phase(s)
  for (size_t i = phaseInfo_.size() - 1; i > 0; i--)
  {
    if (phaseInfo_[i].empty())
      phaseInfo_.resize(i);
    else
      break;
  }
  resetEnabledPhases_();

  // Region destructor cleans up all incoming links
  r.reset();

  return;
}


void
Network::link(const std::string& srcRegionName, const std::string& destRegionName,
              const std::string& linkType, const std::string& linkParams,
              const std::string& srcOutputName, const std::string& destInputName,
              const size_t propagationDelay)
{

  // Find the regions
  if (! regions_.contains(srcRegionName))
    NTA_THROW << "Network::link -- source region '" << srcRegionName << "' does not exist";
  Region_Ptr_t srcRegion = regions_.getByName(srcRegionName);

  if (! regions_.contains(destRegionName))
    NTA_THROW << "Network::link -- dest region '" << destRegionName << "' does not exist";
  Region_Ptr_t destRegion = regions_.getByName(destRegionName);

  // Find the inputs/outputs
  const Spec* srcSpec = srcRegion->getSpec();
  std::string outputName = srcOutputName;
  if (outputName == "")
    outputName = srcSpec->getDefaultOutputName();

  Output* srcOutput = srcRegion->getOutput(outputName);
  if (srcOutput == nullptr)
    NTA_THROW << "Network::link -- output " << outputName
              << " does not exist on region " << srcRegionName;


  const Spec *destSpec = destRegion->getSpec();
  std::string inputName;
  if (destInputName == "")
    inputName = destSpec->getDefaultInputName();
  else
    inputName = destInputName;

  Input* destInput = destRegion->getInput(inputName);
  if (destInput == nullptr)
  {
    NTA_THROW << "Network::link -- input '" << inputName
              << " does not exist on region " << destRegionName;
  }

  // Create the link itself
  auto link = std::make_shared<Link>(linkType, linkParams, srcOutput, destInput, propagationDelay);

  destInput->addLink(link, srcOutput);

  initialized_ = false;
}

void Network::removeLink(const std::string &srcRegionName,
                         const std::string &destRegionName,
                         const std::string &srcOutputName,
                         const std::string &destInputName) {
  // Find the regions
  if (! regions_.contains(srcRegionName))
    NTA_THROW << "Network::unlink -- source region '" << srcRegionName << "' does not exist";
  Region_Ptr_t srcRegion =  regions_.getByName(srcRegionName);

  if (! regions_.contains(destRegionName))
    NTA_THROW << "Network::unlink -- dest region '" << destRegionName << "' does not exist";
  Region_Ptr_t destRegion = regions_.getByName(destRegionName);

  // Find the inputs
  const Spec *srcSpec = srcRegion->getSpec();
  const Spec *destSpec = destRegion->getSpec();
  std::string inputName;
  if (destInputName == "")
    inputName = destSpec->getDefaultInputName();
  else
    inputName = destInputName;

  Input* destInput = destRegion->getInput(inputName);
  if (destInput == nullptr)
  {
    NTA_THROW << "Network::unlink -- input '" << inputName
              << " does not exist on region " << destRegionName;
  }

  std::string outputName = srcOutputName;
  if (outputName == "")
    outputName = srcSpec->getDefaultOutputName();
  Link_Ptr_t link = destInput->findLink(srcRegionName, outputName);

  if (link == nullptr)
    NTA_THROW << "Network::unlink -- no link exists from region " << srcRegionName
              << " output " << outputName << " to region " << destRegionName
              << " input " << destInput->getName();

  // Finally, remove the link
  destInput->removeLink(link);

}

void Network::run(int n)
{
  if (!initialized_)
  {
    initialize();
  }

  if (phaseInfo_.empty())
    return;

  NTA_CHECK(maxEnabledPhase_ < phaseInfo_.size()) << "maxphase: " << maxEnabledPhase_ << " size: " << phaseInfo_.size();

  for(int iter = 0; iter < n; iter++)
  {
    iteration_++;
    NTA_DEBUG << "> Iteration: " << iteration_ << " <";

    // compute on all enabled regions in phase order
    for (UInt32 phase = minEnabledPhase_; phase <= maxEnabledPhase_; phase++)
    {
      for (auto r : phaseInfo_[phase])
      {
        r->prepareInputs();
        r->compute();
      }
    }

    // invoke callbacks
    for (UInt32 i = 0; i < callbacks_.getCount(); i++)
    {
      const std::pair<std::string, callbackItem>& callback = callbacks_.getByIndex(i);
      callback.second.first(this, iteration_, callback.second.second);
    }

    // Refresh all links in the network at the end of every timestamp so that
    // data in delayed links appears to change atomically between iterations
    for (size_t i = 0; i < regions_.getCount(); i++) {
      const Region_Ptr_t r = regions_.getByIndex(i).second;

      for (const auto &inputTuple : r->getInputs()) {
        for (const auto pLink : inputTuple.second->getLinks()) {
          pLink->shiftBufferedData();
        }
      }
    }

  } // End of outer run-loop

  return;
}


void Network::initialize()
{

  /*
   * Do not reinitialize if already initialized.
   * Mostly, this is harmless, but it has a side
   * effect of resetting the max/min enabled phases,
   * which causes havoc if we are in the middle of
   * a computation.
   */
  if (initialized_)
    return;


  /*
   * 1. initialize outputs:
   *   - . Delegated to regions
   */
  for (size_t i = 0; i < regions_.getCount(); i++)
  {
    Region_Ptr_t r = regions_.getByIndex(i).second;
    r->initOutputs();
  }

  /*
   * 2. initialize inputs
   *    - Delegated to regions
   */
  for (size_t i = 0; i < regions_.getCount(); i++)
  {
    Region_Ptr_t r = regions_.getByIndex(i).second;
    r->initInputs();
  }

  /*
   * 3. initialize region/impl
   */
  for (size_t i = 0; i < regions_.getCount(); i++)
  {
    Region_Ptr_t r = regions_.getByIndex(i).second;
    r->initialize();
  }

  /*
   * 4. Enable all phases in the network
   */
  resetEnabledPhases_();


  /*
   * Mark network as initialized.
   */
  initialized_ = true;

}


const Collection<Region_Ptr_t>&
Network::getRegions() const
{
  return regions_;
}


Collection<Link_Ptr_t>
Network::getLinks()
{
  Collection<Link_Ptr_t> links;

  for (UInt32 phase = minEnabledPhase_; phase <= maxEnabledPhase_; phase++)
  {
    for (auto r : phaseInfo_[phase])
    {
      for (auto & input : r->getInputs())
      {
        for (auto & link: input.second->getLinks())
        {
          links.add(link->toString(), link);
        }
      }
    }
  }

  return links;
}

// This function is obsolete.  use setCallback() and unsetCallback().
Collection<Network::callbackItem>& Network::getCallbacks()
{
  return callbacks_;
}

void Network::setCallback(std::string name, runCallbackFunction func, void *arg) {
  if (callbacks_.contains(name))
    NTA_THROW << "SetCallback; item " << name << " already exists.";
    Network::callbackItem itm = std::make_pair(func, arg);
    callbacks_.add(name, itm);
}
void Network::unsetCallback(std::string name) {
  if (callbacks_.contains(name))
    callbacks_.remove(name);
}


void
Network::setMinEnabledPhase(UInt32 minPhase)
{
  if (minPhase >= phaseInfo_.size())
    NTA_THROW << "Attempt to set min enabled phase " << minPhase
              << " which is larger than the highest phase in the network - "
              << phaseInfo_.size() - 1;
  minEnabledPhase_ = minPhase;
}

void
Network::setMaxEnabledPhase(UInt32 maxPhase)
{
  if (maxPhase >= phaseInfo_.size())
    NTA_THROW << "Attempt to set max enabled phase " << maxPhase
              << " which is larger than the highest phase in the network - "
              << phaseInfo_.size() - 1;
  maxEnabledPhase_ = maxPhase;
}

UInt32 Network::getMinEnabledPhase() const { return minEnabledPhase_; }

UInt32 Network::getMaxEnabledPhase() const {  return maxEnabledPhase_; }


void Network::save(std::ostream &f) const {
  // save Network, Region, Links

  f << "Network " << getSerializableVersion() << std::endl;
  f << "{\n";
  f << "iteration: " << iteration_ << "\n";
  f << "Regions: " << "[ " << regions_.getCount() << "\n";

  for (size_t regionIndex = 0; regionIndex < regions_.getCount(); regionIndex++)
  {
      const std::pair<std::string, Region_Ptr_t>& info = regions_.getByIndex(regionIndex);
      Region_Ptr_t r = info.second;
      r->save(f);
  }
  f << "]\n"; // end of regions

  // Save the Links
  // determine the number of links to save.
  Size count = 0;
  for (size_t regionIndex = 0; regionIndex < regions_.getCount(); regionIndex++)
  {
    Region_Ptr_t r = regions_.getByIndex(regionIndex).second;
    const std::map<std::string, Input*> inputs = r->getInputs();
    for (const auto & inputs_input : inputs)
    {
      const std::vector<Link_Ptr_t>& links = inputs_input.second->getLinks();
      count += links.size();
    }
  }

  f << "Links: [ " << count << "\n";

  // Now serialize the links
  for (size_t regionIndex = 0; regionIndex < regions_.getCount(); regionIndex++)
  {
    Region_Ptr_t r = regions_.getByIndex(regionIndex).second;
    const std::map<std::string, Input*> inputs = r->getInputs();
    for (const auto & inputs_input : inputs)
    {
      const std::vector<Link_Ptr_t>& links = inputs_input.second->getLinks();
      for (const auto & links_link : links)
      {
        auto l = links_link;
        l->serialize(f);
      }

    }
  }
  f << "]\n"; // end of links

  f << "}\n"; // end of network
  f << std::endl;
}




void Network::load(std::istream &f) {

  std::string tag;
  int version;
  int minimum_version = 1; // the lowest acceptable version
  Size count;

  // Remove all existing regions and links
  for (size_t regionIndex = 0; regionIndex < regions_.getCount(); regionIndex++)
  {
    Region_Ptr_t r = regions_.getByIndex(regionIndex).second;
    removeRegion(r->getName());
  }
  initialized_ = false;


  f >> tag;
  NTA_CHECK(tag == "Network")  << "Invalid network structure file -- does not contain 'Network' as starting tag.";
  f >> version;
  NTA_CHECK(version >= minimum_version) << "Expecting at least version "
          << minimum_version << " for Network stream.";
  f >> tag;
  NTA_CHECK(tag == "{") << "Expected beginning of a map.";
  f >> tag;
  NTA_CHECK(tag == "iteration:");
  f >> iteration_;

  // Regions
  f >> tag;
  NTA_CHECK(tag == "Regions:");
  f >> tag;
  NTA_CHECK(tag == "[") << "Expected the beginning of a list";
  f >> count;
  for (Size n = 0; n < count; n++)
  {
    Region_Ptr_t r = std::make_shared<Region>(this);
    r->load(f);
    regions_.add(r->getName(), r);

    // We must make a copy of the phases set here because
    // setPhases_ will be passing this back down into
    // the region.
    std::set<UInt32> phases = r->getPhases();
    setPhases_(r, phases);

  }
  f >> tag;
  NTA_CHECK(tag == "]") << "Expected end of list of regions.";


  //  Links
  f >> tag;
  NTA_CHECK(tag == "Links:");
  f >> tag;
  NTA_CHECK(tag == "[") << "Expected beginning of list of links.";
  f >> count;

  for (Size n=0; n < count; n++)
  {
    // Create the link
    auto newLink = std::make_shared<Link>();
    newLink->deserialize(f);

    // Now connect the link to the regions
    const std::string srcRegionName = newLink->getSrcRegionName();
    NTA_CHECK(regions_.contains(srcRegionName)) << "Invalid network structure file -- link specifies source region '"
          << srcRegionName << "' but no such region exists";
    Region_Ptr_t srcRegion = regions_.getByName(srcRegionName);

    const std::string destRegionName = newLink->getDestRegionName();
    NTA_CHECK(regions_.contains(destRegionName)) << "Invalid network structure file -- link specifies destination region '"
                << destRegionName << "' but no such region exists";
    Region_Ptr_t destRegion = regions_.getByName(destRegionName);

    const std::string srcOutputName = newLink->getSrcOutputName();
    Output *srcOutput = srcRegion->getOutput(srcOutputName);
    NTA_CHECK(srcOutput != nullptr) << "Invalid network structure file -- link specifies source output '"
          << srcOutputName << "' but no such name exists";

    const std::string destInputName = newLink->getDestInputName();
    Input *destInput = destRegion->getInput(destInputName);
    NTA_CHECK(destInput != nullptr) << "Invalid network structure file -- link specifies destination input '"
                << destInputName << "' but no such name exists";

    newLink->connectToNetwork(srcOutput, destInput);
    destInput->addLink(newLink, srcOutput);

    // The Links will not be initialized. So must call net.initialize() after load().
  } // links

  f >> tag;
  NTA_CHECK(tag == "]");  // end of links
  f >> tag;
  NTA_CHECK(tag == "}");  // end of network
  f.ignore(1);

  // Post Load operations
  initialize();   //  re-initialize everything
  NTA_CHECK(maxEnabledPhase_ < phaseInfo_.size())
      << "maxphase: " << maxEnabledPhase_ << " size: " << phaseInfo_.size();

  // Note: When serialized, the output buffers are saved
  //       by each RegionImpl.  After restore we need to
  //       copy restored outputs to connected inputs.
  //
  //       Input buffers are not saved, they are restored by
  //       copying from their source output buffers via links.
  //       (If an input is manually set then the input would be
  //       lost after restore.)

  for (size_t i = 0; i < regions_.getCount(); i++) {
    Region_Ptr_t r = regions_.getByIndex(i).second;

    r->prepareInputs();

    // The Link serialization saves all but the first buffer
    // (the most recent) of the Propogation Delay array because
    // that buffer is the same as the most current output.
    // So after restore we need to shift the current
    // outputs into the Propogaton Delay array.
    for (const auto &inputTuple : r->getInputs()) {
      for (const auto pLink : inputTuple.second->getLinks()) {
        pLink->shiftBufferedData();
      }
    }
  }
}

void Network::enableProfiling()
{
  for (size_t i = 0; i < regions_.getCount(); i++)
    regions_.getByIndex(i).second->enableProfiling();
}

void Network::disableProfiling()
{
  for (size_t i = 0; i < regions_.getCount(); i++)
    regions_.getByIndex(i).second->disableProfiling();
}

void Network::resetProfiling()
{
  for (size_t i = 0; i < regions_.getCount(); i++)
    regions_.getByIndex(i).second->resetProfiling();
}


void Network::registerCPPRegion(const std::string name, RegisteredRegionImpl* wrapper)
{
  Region::registerCPPRegion(name, wrapper);
}

void Network::unregisterCPPRegion(const std::string name)
{
  Region::unregisterCPPRegion(name);
}


} // namespace nupic
