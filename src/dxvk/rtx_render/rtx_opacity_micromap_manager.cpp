/*
* Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
#include "rtx_opacity_micromap_manager.h"

#include "rtx.h"
#include "rtx_context.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_options.h"

#include "rtx_imgui.h"

#include "rtx/pass/common_binding_indices.h"

// #define VALIDATION_MODE

#ifdef VALIDATION_MODE
#define omm_validation_assert(x) assert(x)
#else
#define omm_validation_assert(x)
#endif

const VkDeviceSize kOmmBufferAlignment = 16;
const VkDeviceSize kMicromapBufferAlignment = 256;

namespace dxvk {

  XXH64_hash_t calculateMaterialSourceHash(const RtInstance& instance) {
    XXH64_hash_t h = kEmptyHash;
#define ADD_TO_HASH(x) h = XXH64(&x, sizeof(x), h)

    ADD_TO_HASH(instance.getMaterialHash());
    ADD_TO_HASH(instance.surface.alphaState);
    // ToDo: needed?
    ADD_TO_HASH(instance.surface.textureColorArg1Source);
    ADD_TO_HASH(instance.surface.textureColorArg2Source);
    ADD_TO_HASH(instance.surface.textureColorOperation);
    ADD_TO_HASH(instance.surface.textureAlphaArg1Source);
    ADD_TO_HASH(instance.surface.textureAlphaArg2Source);
    ADD_TO_HASH(instance.surface.textureAlphaOperation);
    const uint32_t tFactorAlpha = instance.surface.tFactor;
    ADD_TO_HASH(tFactorAlpha);

    return h;
  }

  DxvkOpacityMicromap::DxvkOpacityMicromap(Rc<DxvkDevice> device) : m_device(device) { }

  DxvkOpacityMicromap::~DxvkOpacityMicromap() {
    if (opacityMicromap != VK_NULL_HANDLE) {
      m_device->vkd()->vkDestroyMicromapEXT(m_device->vkd()->device(), opacityMicromap, nullptr);
      opacityMicromap = VK_NULL_HANDLE;
    }

    opacityMicromapTriangleIndexBuffer = nullptr;
    opacityMicromapBuffer = nullptr;
  }

  OpacityMicromapCacheItem::OpacityMicromapCacheItem() {
    // Default constructor is needed for [] access into OMM cache, but it must not be called
    // for a case when the cache item is not already present in the cache
    assert(0 && "Invalid state. Default constructor for OpacityMicromapCacheItem should never be called.");
    Logger::err("[RTX Opacity Micromap] Encountered inconsistent state. Default constructor for OpacityMicromapCacheItem should never be called.");
  }

  OpacityMicromapCacheItem::OpacityMicromapCacheItem(Rc<DxvkDevice> device,
                                                     OpacityMicromapCacheState _cacheState,
                                                     const uint32_t inputSubdivisionLevel,
                                                     const bool enableVertexAndTextureOperations, 
                                                     uint32_t currentFrameIndex,
                                                     std::list<XXH64_hash_t>::iterator _leastRecentlyUsedListIter,
                                                     const OmmRequest& ommRequest)
    : cacheState(_cacheState)
    , lastUseFrameIndex(currentFrameIndex)
    , leastRecentlyUsedListIter(_leastRecentlyUsedListIter)
    , numTriangles(ommRequest.numTriangles)
    , ommFormat(ommRequest.ommFormat) {
    useVertexAndTextureOperations = enableVertexAndTextureOperations;
    const uint32_t maxSubdivisionLevel =
      ommFormat == VkOpacityMicromapFormatEXT::VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT
      ? device->properties().extOpacityMicromapProperties.maxOpacity2StateSubdivisionLevel
      : device->properties().extOpacityMicromapProperties.maxOpacity4StateSubdivisionLevel;
    subdivisionLevel = std::min(inputSubdivisionLevel, maxSubdivisionLevel);
  }

  bool OpacityMicromapCacheItem::isCompatibleWithOmmRequest(const OmmRequest& ommRequest) {
    return ommRequest.ommFormat == ommFormat &&
           ommRequest.numTriangles == numTriangles;
  }

  VkDeviceSize OpacityMicromapCacheItem::getDeviceSize() const {
    return blasOmmBuffersDeviceSize + arrayBufferDeviceSize;
  }

  OpacityMicromapMemoryManager::OpacityMicromapMemoryManager(
    const Rc<DxvkDevice>& device)
    : m_device(device)
    , m_memoryProperties(device->adapter()->memoryProperties()) {
    // +1 to account for OMMs used in a previous TLAS
    const uint32_t kMaxFramesOMMResourcesAreUsed = kMaxFramesInFlight + 1;

    for (uint32_t i = 0; i < kMaxFramesOMMResourcesAreUsed; i++)
      m_pendingReleaseSize.push_front(0);
  }

  void OpacityMicromapMemoryManager::onFrameStart() {
    VkDeviceSize sizeToRelease = std::min(m_pendingReleaseSize.back(), m_used);
    m_pendingReleaseSize.pop_back();
    m_pendingReleaseSize.push_front(0);

    m_used -= sizeToRelease;
  }

  void OpacityMicromapMemoryManager::updateMemoryBudget(Rc<DxvkContext> ctx, const OpacityMicromapSettings& settings) {

    // Gather runtime vidmem stats
    VkDeviceSize vidmemSize = 0;
    VkDeviceSize vidmemUsedSize = static_cast<VkDeviceSize>(settings.minFreeVidmemMBToNotAllocate) * 1024 * 1024;
    
    DxvkMemoryAllocator& memoryManager = ctx->getCommonObjects()->memoryManager();
    const VkPhysicalDeviceMemoryProperties& memoryProperties = memoryManager.getMemoryProperties();
    const std::array<DxvkMemoryHeap, VK_MAX_MEMORY_HEAPS>& memoryHeaps = memoryManager.getMemoryHeaps();

    for (uint32_t i = 0; i < memoryProperties.memoryHeapCount; i++) {
      if (memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
        vidmemSize += memoryHeaps[i].budget > 0 ? memoryHeaps[i].budget : memoryHeaps[i].properties.size;
        vidmemUsedSize += memoryHeaps[i].stats.totalUsed();
      }
    }

    const VkDeviceSize vidmemFreeSize = vidmemSize - std::min(vidmemUsedSize, vidmemSize);

    // Calculate a new budget given the runtime vidmem stats
    VkDeviceSize maxAllowedBudget = std::min(
      static_cast<VkDeviceSize>(static_cast<double>(settings.maxVidmemSizePercentage) * vidmemSize), 
      static_cast<VkDeviceSize>(settings.maxBudgetSizeMB) * 1024 * 1024);
    VkDeviceSize newBudget = std::min(vidmemFreeSize, maxAllowedBudget);

    if (newBudget < static_cast<VkDeviceSize>(settings.minBudgetSizeMB) * 1024 * 1024)
      newBudget = 0;
    
    // Update the budget
    if (newBudget != m_budget) {
      if (m_budget == 0)
        ONCE(Logger::info("[RTX Opacity Micromap] Setting budget from 0 to greater than 0."));
      if (newBudget == 0)
        ONCE(Logger::info("[RTX Opacity Micromap] Free Vidmem dropped below a cutoff. Setting budget to 0."));

      m_budget = newBudget;
    }
  }

  bool OpacityMicromapMemoryManager::allocate(VkDeviceSize size) {
    if (size > getAvailable()) {
      ONCE(Logger::info(str::format("[RTX Opacity Micromap] Out of memory budget. Requested: ", size, " bytes. Free: ", getAvailable(), " bytes, Budget: ", getBudget(), " bytes")));
      return false;
    }

    m_used += size;

    return true;
  }

  VkDeviceSize OpacityMicromapMemoryManager::getAvailable() const {
    return m_budget - std::min(m_used, m_budget);
  }

  void OpacityMicromapMemoryManager::release(VkDeviceSize size) { 
    m_pendingReleaseSize.back() += size;
  }

  void OpacityMicromapMemoryManager::releaseAll() {
    release(m_used);
  }

  float OpacityMicromapMemoryManager::calculateUsageRatio() const {
    return m_used / static_cast<float>(m_budget);
  }

  VkDeviceSize OpacityMicromapMemoryManager::calculatePendingAvailableSize() const {
    return std::min(getAvailable() + calculatePendingReleasedSize(), m_budget);
  }

  VkDeviceSize OpacityMicromapMemoryManager::calculatePendingReleasedSize() const {
    VkDeviceSize totalSizeToRelease = 0;
    for (auto& sizeToRelease : m_pendingReleaseSize)
      totalSizeToRelease += sizeToRelease;

    return totalSizeToRelease;
  }

  VkDeviceSize OpacityMicromapMemoryManager::getNextPendingReleasedSize() const {
    return m_pendingReleaseSize.back();
  }

  OpacityMicromapManager::OpacityMicromapManager(Rc<DxvkDevice> device)
    : m_device(device)
    , m_memoryManager(device)
    , m_scratchAllocator(device, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR) {
    initSettings();
  }

  OmmRequest::OmmRequest(const RtInstance& _instance, const InstanceManager& instanceManager, const OpacityMicromapSettings& settings, uint32_t _quadSliceIndex)
    : instance(_instance)
    , quadSliceIndex(_quadSliceIndex) {
    ommSrcHash = calculateMaterialSourceHash(instance);

    if (isBillboardOmmRequest()) {
      const IntersectionBillboard& billboard = instanceManager.getBillboards()[instance.getFirstBillboardIndex() + quadSliceIndex];
      ommSrcHash = XXH64(&billboard.texCoordHash, sizeof(billboard.texCoordHash), ommSrcHash);
      ommSrcHash = XXH64(&billboard.vertexOpacityHash, sizeof(billboard.vertexOpacityHash), ommSrcHash);
      numTriangles = 2;
    } 
    else {
      numTriangles = instance.getBlas()->modifiedGeometryData.calculatePrimitiveCount();
      
      assert(instance.getTexcoordHash() != kEmptyHash);

      ommSrcHash = XXH64(&instance.getTexcoordHash(), sizeof(instance.getTexcoordHash()), ommSrcHash);
      // ToDo: is this already included in any of the previous hashes added up to this point?
      ommSrcHash = XXH64(&instance.surface.textureTransform, sizeof(instance.surface.textureTransform), ommSrcHash);
    }

    ommSrcHash = XXH64(&numTriangles, sizeof(numTriangles), ommSrcHash);

    // Select OmmFormat for the OMM request and add it to the hash
    {
      ommFormat = VK_OPACITY_MICROMAP_FORMAT_4_STATE_EXT;

      auto& alphaState = instance.surface.alphaState;

      if (settings.allow2StateOpacityMicromaps && (
        isBillboardOmmRequest() ||
        (!alphaState.isFullyOpaque && (alphaState.isParticle || alphaState.isDecal)) || alphaState.emissiveBlend))
        ommFormat = VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT;

      if (settings.force2StateOpacityMicromaps)
        ommFormat = VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT;

      ommSrcHash = XXH64(&ommFormat, sizeof(ommFormat), ommSrcHash);
    }
  }

  OpacityMicromapManager::CachedSourceData::~CachedSourceData() {
    omm_validation_assert(!instance && "Instance has not been unlinked");
  }

  void OpacityMicromapManager::CachedSourceData::initialize(const OmmRequest& ommRequest, std::unordered_map<XXH64_hash_t, InstanceOmmRequests>& instanceOmmRequests) {
    setInstance(&ommRequest.instance, instanceOmmRequests);

    numTriangles = ommRequest.numTriangles;

    if (ommRequest.isBillboardOmmRequest()) {
      // ToDo: add compiler check support to ensure the right values are specified here
      triangleOffset = 2 * ommRequest.quadSliceIndex;
    } else {
      triangleOffset = 0;
    }
  }

  void OpacityMicromapManager::CachedSourceData::setInstance(const RtInstance* _instance, std::unordered_map<XXH64_hash_t, InstanceOmmRequests>& instanceOmmRequests, bool deleteParentInstanceIfEmpty) {
    omm_validation_assert(instance != _instance && "Redundant call setting the same instance twice.");

    if (instance && _instance)
      setInstance(nullptr, instanceOmmRequests, deleteParentInstanceIfEmpty);

    if (_instance) {
      instanceOmmRequests[_instance->getOpacityMicromapSourceHash()].numActiveRequests += 1;
    } 
    // instance should always be valid at this point, but let's check on previous instance being actually valid before unlinking it
    else if (instance) {
      auto instanceOmmRequestsIter = instanceOmmRequests.find(instance->getOpacityMicromapSourceHash());
      omm_validation_assert(instanceOmmRequestsIter->second.numActiveRequests > 0);
      instanceOmmRequestsIter->second.numActiveRequests -= 1;
      if (deleteParentInstanceIfEmpty && instanceOmmRequestsIter->second.numActiveRequests == 0)
        instanceOmmRequests.erase(instanceOmmRequestsIter);
    }

    instance = _instance;
  }  
  
  void OpacityMicromapManager::initSettings() {
    m_settings.maxOmmBuildRequests = RtxOptionSettings::maxOmmBuildRequests();
    m_settings.numFramesAtStartToBuildWithHighWorkload = RtxOptionSettings::numFramesAtStartToBuildWithHighWorkload();
    m_settings.workloadHighWorkloadMultiplier = RtxOptionSettings::workloadHighWorkloadMultiplier();
    m_settings.maxVidmemSizePercentage = RtxOptionSettings::maxVidmemSizePercentage();
    m_settings.minFreeVidmemMBToNotAllocate = RtxOptionSettings::minFreeVidmemMBToNotAllocate();
    m_settings.minBudgetSizeMB = RtxOptionSettings::minBudgetSizeMB();
    m_settings.maxBudgetSizeMB = RtxOptionSettings::maxBudgetSizeMB();
    m_settings.ommBuildRequest_minInstanceFrameAge = RtxOptionSettings::ommBuildRequest_minInstanceFrameAge();
    m_settings.ommBuildRequest_minNumFramesRequested = RtxOptionSettings::ommBuildRequest_minNumFramesRequested();
    m_settings.ommBuildRequest_minNumRequests = RtxOptionSettings::ommBuildRequest_minNumRequests();
    m_settings.subdivisionLevel = RtxOptionSettings::subdivisionLevel();
    m_settings.enableResetEveryFrame = RtxOptionSettings::enableResetEveryFrame();
    m_settings.enableParticles = RtxOptionSettings::enableParticles();
    m_settings.conservativeEstimationMaxTexelTapsPerMicroTriangle = RtxOptionSettings::conservativeEstimationMaxTexelTapsPerMicroTriangle();
  }

  void OpacityMicromapManager::destroyOmmData(const OpacityMicromapCache::iterator& ommCacheItemIter, bool destroyParentInstanceOmmRequestContainer) {
    const XXH64_hash_t ommSrcHash = ommCacheItemIter->first;
    const OpacityMicromapCacheState ommCacheState = ommCacheItemIter->second.cacheState;

#ifdef VALIDATION_MODE
    Logger::warn(str::format("[RTX Opacity Micromap] Destroying ", ommSrcHash, " on thread_id ", std::this_thread::get_id()));
#endif

    switch (ommCacheState) {
    case OpacityMicromapCacheState::eStep0_Unprocessed:
    case OpacityMicromapCacheState::eStep1_Baking:
      m_unprocessedList.remove(ommSrcHash);
      break;
    case OpacityMicromapCacheState::eStep2_Baked:
      m_bakedList.remove(ommSrcHash);
      break;
    case OpacityMicromapCacheState::eStep3_Built:
      m_builtList.remove(ommSrcHash);
      break;
    case OpacityMicromapCacheState::eStep4_Ready:
      m_readyList.remove(ommSrcHash);
      break;
    default:
      omm_validation_assert(0);
      break;
    }

    if (ommCacheState <= OpacityMicromapCacheState::eStep2_Baked)
      deleteCachedSourceData(ommSrcHash, ommCacheState, destroyParentInstanceOmmRequestContainer);

    m_leastRecentlyUsedList.erase(ommCacheItemIter->second.leastRecentlyUsedListIter);
    m_memoryManager.release(ommCacheItemIter->second.getDeviceSize());
    m_ommCache.erase(ommCacheItemIter);
  }

  void OpacityMicromapManager::destroyOmmData(XXH64_hash_t ommSrcHash) {
    destroyOmmData(m_ommCache.find(ommSrcHash));
  }

  void OpacityMicromapManager::destroyInstance(const RtInstance& instance, bool forceDestroy) {
    // Don't destroy the container as it's being used to iterate through below
    const bool destroyParentInstanceOmmRequestContainer = false;

    auto destroyCachedData = [&](XXH64_hash_t ommSrcHash) {
      auto ommCacheIterator = m_ommCache.find(ommSrcHash);

      // Unknown element, ignore it
      if (ommCacheIterator == m_ommCache.end())
        return;

      const OpacityMicromapCacheState ommCacheState = ommCacheIterator->second.cacheState;

      if (!forceDestroy) {
        switch (ommCacheState) {
          // Continue with destruction of unbaked items
        case OpacityMicromapCacheState::eStep0_Unprocessed:
          break;

          // If the OMM data has been at least partially baked keep it in the cache
        case OpacityMicromapCacheState::eStep1_Baking:
          // Remove partially baked OMM items from to be baked list until a new instance is linked with it again
          m_unprocessedList.remove(ommSrcHash);
          deleteCachedSourceData(ommSrcHash, ommCacheState, destroyParentInstanceOmmRequestContainer);
          return;
        case OpacityMicromapCacheState::eStep2_Baked:
        case OpacityMicromapCacheState::eStep3_Built:
        case OpacityMicromapCacheState::eStep4_Ready:
          return;

        default:
          // Continue with destruction
          omm_validation_assert(0);
          break;
        }
      }

      // Note: invalidates the omm cache iterator
      destroyOmmData(ommCacheIterator, destroyParentInstanceOmmRequestContainer);
    };

    XXH64_hash_t ommSrcHash = instance.getOpacityMicromapSourceHash();

    // Invalid hash, ignore it
    if (ommSrcHash == kEmptyHash)
      return;

    auto instanceOmmRequestsIter = m_instanceOmmRequests.find(ommSrcHash);

    // Untracked instance, ignore it
    if (instanceOmmRequestsIter == m_instanceOmmRequests.end())
      return;

    // Destroy all OMM requests associated with the instance
    static_assert(destroyParentInstanceOmmRequestContainer == false);
    for (auto& ommRequest : instanceOmmRequestsIter->second.ommRequests)
      destroyCachedData(ommRequest.ommSrcHash);

    m_instanceOmmRequests.erase(instanceOmmRequestsIter);
  }

  uint32_t OpacityMicromapManager::calculateNumMicroTriangles(uint16_t subdivisionLevel) {
    return static_cast<uint32_t>(round(pow(4, subdivisionLevel)));
  }

  void OpacityMicromapManager::clear() {
    m_unprocessedList.clear();
    m_bakedList.clear();
    m_builtList.clear();
    m_readyList.clear();

    m_leastRecentlyUsedList.clear();
    m_ommCache.clear();
    m_cachedSourceData.clear();
    m_ommBuildRequestStatistics.clear();

    m_instanceOmmRequests.clear();

    m_memoryManager.releaseAll();
    m_amountOfMemoryMissing = 0;

    // There's no need to clear m_blackListedList
  }
  
  void OpacityMicromapManager::showImguiSettings() const {

    const static ImGuiSliderFlags sliderFlags = ImGuiSliderFlags_AlwaysClamp;
    const static ImGuiTreeNodeFlags collapsingHeaderFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_CollapsingHeader;
    const static ImGuiTreeNodeFlags collapsingHeaderClosedFlags = ImGuiTreeNodeFlags_CollapsingHeader;

#define ADVANCED(x) if (m_settings.showAdvancedOptions) x

    ImGui::Checkbox("Show Advanced Settings", &m_settings.showAdvancedOptions);
    ImGui::Checkbox("Enable Binding", &m_settings.enableBinding);
    ADVANCED(ImGui::Checkbox("Enable Baking Arrays", &m_settings.enableBakingArrays));
    ADVANCED(ImGui::Checkbox("Enable Building", &m_settings.enableBuilding));

    ImGui::Checkbox("Reset Every Frame", &m_settings.enableResetEveryFrame);

    // Stats
    if (ImGui::CollapsingHeader("Statistics", collapsingHeaderFlags)) {
      ImGui::Indent();
      ImGui::Text("# Bound/Requested OMMs: %d/%d", m_numBoundOMMs, m_numRequestedOMMBindings);
      ADVANCED(ImGui::Text("# Staged Requested Items: %d", m_ommBuildRequestStatistics.size()));
      ADVANCED(ImGui::Text("# Unprocessed Items: %d", m_unprocessedList.size()));
      ADVANCED(ImGui::Text("# Baked Items: %d", m_bakedList.size()));
      ADVANCED(ImGui::Text("# Built Items: %d", m_builtList.size()));
      ImGui::Text("# Ready Items: %d", m_readyList.size());
      ADVANCED(ImGui::Text("# Cache Items: %d", m_ommCache.size()));
      ADVANCED(ImGui::Text("# Black Listed Items: %d", m_blackListedList.size()));
      ImGui::Text("VRAM usage/budget [MB]: %d/%d", m_memoryManager.getUsed() / (1024 * 1024), m_memoryManager.getBudget() / (1024 * 1024));

      ADVANCED(ImGui::Text("# Baked uTriagles [million]: %.1f", m_numMicroTrianglesBaked / 1e6 ));

      ADVANCED(ImGui::Text("# Built uTriagles [million]: %.1f", m_numMicroTrianglesBuilt / 1e6));
      ImGui::Unindent();
    }

    ADVANCED(
      if (ImGui::CollapsingHeader("Scene", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        ImGui::Checkbox("Split Billboard Geometry", &m_settings.splitBillboardGeometry);
        ImGui::DragInt("Max Allowed Billboards Per Instance To Split", &m_settings.maxAllowedBillboardsPerInstanceToSplit, 1.f, 0, 4096, "%d", sliderFlags);
        ImGui::Unindent();
      });
    
    if (ImGui::CollapsingHeader("Cache", collapsingHeaderClosedFlags)) {
      ImGui::Indent();
      ImGui::DragFloat("Budget: Max Vidmem Size %", &m_settings.maxVidmemSizePercentage, 0.001f, 0.0f, 1.f, "%.3f", sliderFlags);
      ADVANCED(ImGui::DragInt("Budget: Min Required Size [MB]", &m_settings.minBudgetSizeMB, 8.f, 0, 256 * 1024, "%d", sliderFlags));
      ImGui::DragInt("Budget: Max Allowed Size [MB]", &m_settings.maxBudgetSizeMB, 8.f, 0, 256 * 1024, "%d", sliderFlags);
      ImGui::DragInt("Budget: Min Vidmem Free To Not Allocate [MB]", &m_settings.minFreeVidmemMBToNotAllocate, 16.f, 0, 256 * 1024, "%d", sliderFlags);
      ADVANCED(ImGui::DragInt("Min Usage Frame Age Before Eviction", &m_settings.minUsageFrameAgeBeforeEviction, 1.f, 0, 60 * 3600, "%d", sliderFlags));
      ImGui::Unindent();
    }


    if (ImGui::CollapsingHeader("Requests Filter", collapsingHeaderClosedFlags)) {
      ImGui::Indent();
      ImGui::Checkbox("Animated Instances", &m_settings.enableAnimatedInstances);
      ImGui::Checkbox("Particles", &m_settings.enableParticles);
      ADVANCED(ImGui::Checkbox("Custom Filters for Billboards", &m_settings.customBuildRequestFilteringForBillboards));
      
      ADVANCED(ImGui::DragInt("Max Staged Requests", &m_settings.maxOmmBuildRequests, 1.f, 1, 1000 * 1000, "%d", sliderFlags));
      // ToDo: we don't support setting this to 0 at the moment, should revisit later
      ADVANCED(ImGui::DragInt("Min Instance Frame Age", &m_settings.ommBuildRequest_minInstanceFrameAge, 1.f, 0, 200, "%d", sliderFlags));
      ADVANCED(ImGui::DragInt("Min Num Frames Requested", &m_settings.ommBuildRequest_minNumFramesRequested, 1.f, 0, 200, "%d", sliderFlags));
      ADVANCED(ImGui::DragInt("Max Request Frame Age", &m_settings.ommBuildRequest_maxRequestFramesAge, 1.f, 0, 60 * 3600, "%d", sliderFlags));
      ADVANCED(ImGui::DragInt("Min Num Requests", &m_settings.ommBuildRequest_minNumRequests, 1.f, 1, 1000, "%d", sliderFlags));
      ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("Building", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      // Note: 2 is minimum to ensure # microtriangle size is a multiple of 1 byte to ensure cross triangle alignment requirement
      ImGui::DragInt("Subdivision Level", &m_settings.subdivisionLevel, 1.f, 2, 11, "%d", sliderFlags);
      ADVANCED(ImGui::Checkbox("Vertex, Texture Ops & Emissive Blending", &m_settings.enableVertexAndTextureOperations));
      ADVANCED(ImGui::Checkbox("Allow 2 State Opacity Micromaps", &m_settings.allow2StateOpacityMicromaps));
      ADVANCED(ImGui::Checkbox("Force 2 State Opacity Micromaps", &m_settings.force2StateOpacityMicromaps));

      ADVANCED(ImGui::DragFloat("Decals: Min Resolve Transparency Threshold", &m_settings.decalsMinResolveTransparencyThreshold, 0.001f, 0.0f, 1.f, "%.3f", sliderFlags));

      ADVANCED(ImGui::DragInt("Max # of uTriangles to Bake [Million per Second]", &m_settings.maxMicroTrianglesToBakeMillionPerSecond, 1.f, 1, 65536, "%d", sliderFlags));
      ADVANCED(ImGui::DragInt("Max # of uTriangles to Build [Million per Second]", &m_settings.maxMicroTrianglesToBuildMillionPerSecond, 1.f, 1, 65536, "%d", sliderFlags));
      ADVANCED(ImGui::DragInt("# Frames with High Workload Multiplier at Start", &m_settings.numFramesAtStartToBuildWithHighWorkload, 1.f, 0, 100000, "%d", sliderFlags));
      ADVANCED(ImGui::DragInt("High Workload Multiplier", &m_settings.workloadHighWorkloadMultiplier, 1.f, 1, 1000, "%d", sliderFlags));

      if (ImGui::CollapsingHeader("Conservative Estimation", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        ImGui::Checkbox("Enable", &m_settings.enableConservativeEstimation);
        ADVANCED(ImGui::DragInt("Max Texel Taps Per uTriangle", &m_settings.conservativeEstimationMaxTexelTapsPerMicroTriangle, 16.f, 1, 256 * 256, "%d", sliderFlags);
        ImGui::Unindent());
      }

      ImGui::Unindent();
    }
  }

  bool OpacityMicromapManager::checkIsOpacityMicromapSupported(Rc<DxvkDevice> device) {
    bool isOpacityMicromapSupported = device->extensions().khrSynchronization2 &&
                                      device->extensions().extOpacityMicromap;

    if (device->instance()->areVulkanValidationLayersEnabled() && isOpacityMicromapSupported) {
      Logger::warn(str::format("[RTX] Opacity Micromap vendor extension is not compatible with VK Validation Layers. Disabling Opacity Micromap extension."));
      isOpacityMicromapSupported = false;
    }

    return isOpacityMicromapSupported;
  }


  InstanceEventHandler OpacityMicromapManager::getInstanceEventHandler() {
    InstanceEventHandler instanceEvents(this);
    instanceEvents.onInstanceAddedCallback = [this](const RtInstance& instance) { onInstanceAdded(instance); };
    instanceEvents.onInstanceAddedCallback = [this](const RtInstance& instance) { onInstanceAdded(instance); };
    instanceEvents.onInstanceUpdatedCallback = [this](const RtInstance& instance, const RtSurfaceMaterial& material, bool hasTransformChanged, bool hasVerticesChanged) { onInstanceUpdated(instance, material, hasTransformChanged, hasVerticesChanged); };
    instanceEvents.onInstanceDestroyedCallback = [this](const RtInstance& instance) { onInstanceDestroyed(instance); };
    return instanceEvents;
  }

  void OpacityMicromapManager::onInstanceAdded(const RtInstance& instance) {
    // Do nothing, all instances with their finalized material state are processed
    // after the frame submissions
  }

  void OpacityMicromapManager::onInstanceUpdated(const RtInstance& instance, 
                                                 const RtSurfaceMaterial& material, 
                                                 const bool hasTransformChanged, 
                                                 const bool hasVerticesChanged) {
    // Do nothing, all instances with their finalized material state are processed
    // after the frame submission
  }

  void OpacityMicromapManager::onInstanceDestroyed(const RtInstance& instance) {
    destroyInstance(instance);
  }

  bool OpacityMicromapManager::doesInstanceUseOpacityMicromap(const RtInstance& instance) const {
    if (instance.getTexcoordHash() == kEmptyHash) {
      ONCE(Logger::info("[RTX Opacity Micromap] No texcoord hash detected. Ignoring the Opacity Micromap request."));
      return false;
    }

    bool useOpacityMicromap = false;

    auto& surface = instance.surface;
    auto& alphaState = instance.surface.alphaState;

    // Find valid OMM candidates
    if (
      (!alphaState.isFullyOpaque && alphaState.isParticle) || alphaState.emissiveBlend) {
      // Alpha-blended and emissive particles go to the separate "unordered" TLAS as non-opaque geometry
      useOpacityMicromap = true;
    } else if (instance.getMaterialType() == RtSurfaceMaterialType::Opaque && 
               !instance.surface.alphaState.isFullyOpaque && 
               instance.surface.alphaState.isBlendingDisabled) {
      // Alpha-tested geometry goes to the primary TLAS as non-opaque geometry with potential duplicate hits.
      useOpacityMicromap = true;
    } else if (instance.getMaterialType() == RtSurfaceMaterialType::Opaque && !alphaState.isFullyOpaque) {
      useOpacityMicromap = true;
    } else if (instance.getMaterialType() == RtSurfaceMaterialType::Translucent) {
      // Translucent (e.g. glass) geometry goes to the primary TLAS as non-opaque geometry with no duplicate hits.
    } else if (instance.getMaterialType() == RtSurfaceMaterialType::RayPortal) {
      // Portals go to the primary TLAS as opaque.
      useOpacityMicromap = true;
    } else {
      // All other fully opaques go to the primary TLAS as opaque.
    }

    // Process Opacity Micromap enablement
    {
      useOpacityMicromap &= !instance.isAnimated() || m_settings.enableAnimatedInstances;
      useOpacityMicromap &= !alphaState.isParticle || m_settings.enableParticles;
      if (m_settings.splitBillboardGeometry)
        useOpacityMicromap &= instance.getBillboardCount() <= m_settings.maxAllowedBillboardsPerInstanceToSplit;
    }

    // Check if it needs per uTriangle opacity data
    if (useOpacityMicromap) {
      // ToDo: cover all cases to avoid OMM generation unnecessarily
      if (alphaState.alphaTestType == AlphaTestType::kAlways && alphaState.blendType == BlendType::kAlpha) {
        float tFactorAlpha = ((surface.tFactor >> 24) & 0xff) / 255.f;
        switch (surface.textureAlphaOperation) {
        case DxvkRtTextureOperation::SelectArg1:
          if (surface.textureAlphaArg1Source == RtTextureArgSource::TFactor)
            useOpacityMicromap &= tFactorAlpha > RtxOptions::Get()->getResolveTransparencyThreshold();
          break;
        case DxvkRtTextureOperation::SelectArg2:
          if (surface.textureAlphaArg2Source == RtTextureArgSource::TFactor)
            useOpacityMicromap &= tFactorAlpha > RtxOptions::Get()->getResolveTransparencyThreshold();
          break;
        }
      }
    }

    return useOpacityMicromap;
  }

  void OpacityMicromapManager::onBlasBuild(Rc<DxvkCommandList> cmdList) {
    addBarriersForBuiltOMMs(cmdList);
  }

  static bool isIndexOfFullyResidentTexture(uint32_t index, const std::vector<TextureRef>& textures) {
    return index != BINDING_INDEX_INVALID && textures[index].isFullyResident();
  }

  bool OpacityMicromapManager::areInstanceTexturesResident(const RtInstance& instance, const std::vector<TextureRef>& textures) const {
    // Opacity map not loaded yet
    if (!isIndexOfFullyResidentTexture(instance.getAlbedoOpacityTextureIndex(), textures))
      return false;

    // RayPortal materials use two opacity maps, see if the second one is already loaded
    if (instance.getMaterialType() == RtSurfaceMaterialType::RayPortal &&
        !isIndexOfFullyResidentTexture(instance.getSecondaryOpacityTextureIndex(), textures))
      return false;

    return true;
  }

  void OpacityMicromapManager::updateSourceHash(RtInstance& instance, XXH64_hash_t ommSrcHash) {
    XXH64_hash_t prevOmmSrcHash = instance.getOpacityMicromapSourceHash();

    if (prevOmmSrcHash != kEmptyHash && ommSrcHash != prevOmmSrcHash) {
      // Valid source hash changed, deassociate instance from the previous hash
      destroyInstance(instance);
    }

    instance.setOpacityMicromapSourceHash(ommSrcHash);
  }

  std::unordered_map<XXH64_hash_t, OpacityMicromapManager::CachedSourceData>::iterator OpacityMicromapManager::registerCachedSourceData(const OmmRequest& ommRequest) {

    auto sourceDataIter = m_cachedSourceData.insert({ ommRequest.ommSrcHash, CachedSourceData() }).first;
    CachedSourceData& sourceData = sourceDataIter->second;

    sourceData.initialize(ommRequest, m_instanceOmmRequests);

    if (sourceData.numTriangles == 0) {
      ONCE(Logger::warn("[RTX Opacity Micromap] Input geometry has 0 triangles. Ignoring the build request."));
      // Unlink the instance
      sourceData.setInstance(nullptr, m_instanceOmmRequests);
      m_cachedSourceData.erase(sourceDataIter);
 
      return m_cachedSourceData.end();
    }

    return sourceDataIter;
  }

  void OpacityMicromapManager::deleteCachedSourceData(std::unordered_map<XXH64_hash_t, OpacityMicromapManager::CachedSourceData>::iterator sourceDataIter, OpacityMicromapCacheState ommCacheState, bool destroyParentInstanceOmmRequestContainer) {
    if (ommCacheState <= OpacityMicromapCacheState::eStep1_Baking)
      sourceDataIter->second.setInstance(nullptr, m_instanceOmmRequests, destroyParentInstanceOmmRequestContainer);
    m_cachedSourceData.erase(sourceDataIter);
  }

  void OpacityMicromapManager::deleteCachedSourceData(XXH64_hash_t ommSrcHash, OpacityMicromapCacheState ommCacheState, bool destroyParentInstanceOmmRequestContainer) {
    auto sourceDataIter = m_cachedSourceData.find(ommSrcHash);
    if (sourceDataIter != m_cachedSourceData.end())
      deleteCachedSourceData(sourceDataIter, ommCacheState, destroyParentInstanceOmmRequestContainer);
  }

  // Returns true if a new OMM build request was accepted
  bool OpacityMicromapManager::addNewOmmBuildRequest(RtInstance& instance, const OmmRequest& ommRequest) {
    
    // Skip processing if there's no available memory backing
    if (m_memoryManager.getBudget() == 0) {
      ONCE(Logger::warn("[RTX Opacity Micromap] OMM baker ran out of memory, and could not complete request."));
      return false;
    }

    // Prevent host getting overloaded
    if (m_ommBuildRequestStatistics.size() >= m_settings.maxOmmBuildRequests)
      return false;

    XXH64_hash_t ommSrcHash = ommRequest.ommSrcHash;

    // Check if the request passes OMM build request filter settings
    {
      // Ignore non-reference view model instance requests for adding new OMM requests
      // Their OMM data will be generated via OMM requests for reference ViewModel instances instead
      // Non-reference view mode instances can only bind available OMMs. 
      // The reason why they cannot be registered for building is that instance manager 
      // does not call destroyInstance callbacks when they are destroyed, plus reference instances
      // are kept across frames which is better for OMM building with a per frame budget.
      if (instance.isViewModelNonReference() ||
          RtxOptions::Get()->shouldOpacityMicromapIgnoreTexture(instance.getMaterialDataHash()) ||
          // Ignore black listed OMM source hashes
          m_blackListedList.find(ommSrcHash) != m_blackListedList.end())
        return false;
  
      uint32_t minInstanceFrameAge = m_settings.ommBuildRequest_minInstanceFrameAge;
      uint32_t minNumRequests = m_settings.ommBuildRequest_minNumRequests;
      uint32_t minNumFramesRequested = m_settings.ommBuildRequest_minNumFramesRequested;

      if (instance.getBillboardCount() > 0 && m_settings.customBuildRequestFilteringForBillboards) {
        // Lower the filter requirements for particles since they are dynamic
        // But still we want to avoid baking particles that do not get reused for now
        minInstanceFrameAge = 0;
        minNumRequests = 2;
        minNumFramesRequested = 0;
      }

      if (!ommRequest.isBillboardOmmRequest()) {
        const uint32_t currentFrameIndex = m_device->getCurrentFrameId();

        OMMBuildRequestStatistics& ommBuildRequestStatistics = m_ommBuildRequestStatistics[ommSrcHash];
        ommBuildRequestStatistics.numTimesRequested = 1 + std::min<uint16_t>(UINT16_MAX - 1, ommBuildRequestStatistics.numTimesRequested);

        if (currentFrameIndex != ommBuildRequestStatistics.lastRequestFrameId) {
          ommBuildRequestStatistics.lastRequestFrameId = currentFrameIndex;
          ommBuildRequestStatistics.numFramesRequested = 1 + std::min<uint16_t>(UINT16_MAX - 1, minNumFramesRequested);
        }

        if (instance.getFrameAge() < minInstanceFrameAge)
          return false;

        if (ommBuildRequestStatistics.numTimesRequested < minNumRequests ||
            ommBuildRequestStatistics.numFramesRequested < minNumFramesRequested)
          return false;

        // Request passed the check, don't track statistics for it no more
        m_ommBuildRequestStatistics.erase(ommSrcHash);
      }
    }

    if (!insertToUnprocessedList(ommRequest))
      return false;

    // Place the element to the end of the LRU list, and thus marking it as most recent 
    m_leastRecentlyUsedList.emplace_back(ommSrcHash);
    auto lastElementIterator = std::next(m_leastRecentlyUsedList.end(), -1);
    m_ommCache.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(ommSrcHash),
      std::forward_as_tuple(m_device, OpacityMicromapCacheState::eStep0_Unprocessed, m_subdivisionLevel, 
                            m_enableVertexAndTextureOperations, m_device->getCurrentFrameId(),
                            lastElementIterator, ommRequest));

    return true;
  }
  
  bool OpacityMicromapManager::insertToUnprocessedList(const OmmRequest& ommRequest) {
    XXH64_hash_t ommSrcHash = ommRequest.ommSrcHash;

    auto sourceDataIter = registerCachedSourceData(ommRequest);

    if (sourceDataIter == m_cachedSourceData.end())
      return false;

    CachedSourceData& sourceData = sourceDataIter->second;

    // Billboard requests go to the end since they are expected to be changed at high frequency and trigger a lot of builds.
    // Therefore, we want to prioritize building ommRequests that passed standard OMM registration filter tests first
    if (!ommRequest.isBillboardOmmRequest()) {
      // Add the OMM request to the unprocessed list according to the numTriangle count in an ascending order 
      // so that requests with least triangles are processed first and thus with lower overall latency
      for (auto itemIter = m_unprocessedList.begin(); itemIter != m_unprocessedList.end(); itemIter++) {

        XXH64_hash_t itemOmmSrcHash = *itemIter;

        CachedSourceData& itemSourceData = m_cachedSourceData[itemOmmSrcHash];

        if (sourceData.numTriangles < itemSourceData.numTriangles ||
            // insert in front of any billboard requests
            itemSourceData.getInstance()->getBillboardCount() > 0) {
          m_unprocessedList.insert(itemIter, ommSrcHash);
          return true;
        }
      }
    }

    m_unprocessedList.emplace_back(ommSrcHash);

    return true;
  }

  void OpacityMicromapManager::generateInstanceOmmRequests(RtInstance& instance, 
                                                           const InstanceManager& instanceManager, 
                                                           std::vector<OmmRequest>& ommRequests) {

    const uint32_t numOmmRequests = std::max(m_settings.splitBillboardGeometry ? instance.getBillboardCount() : 1u, 1u);
    ommRequests.reserve(numOmmRequests);
    XXH64_hash_t ommSrcHash;  // Compound hash for the instance

    // Create all OmmRequest objects corresponding to the instance
    if (instance.getBillboardCount() > 0 && m_settings.splitBillboardGeometry) {
      const uint32_t numTriangles = instance.getBlas()->modifiedGeometryData.calculatePrimitiveCount();
      assert((numTriangles & 1) == 0 &&
             "Only compound omms consisting of multiples of quads are supported");

      std::vector<XXH64_hash_t> ommSrcHashes;
      ommSrcHashes.reserve(numOmmRequests);

      for (uint32_t i = 0; i < instance.getBillboardCount(); i++) {
        OmmRequest ommRequest(instance, instanceManager, m_settings, i);

        // Only track unique omm requests
        if (std::find(ommSrcHashes.begin(), ommSrcHashes.end(), ommRequest.ommSrcHash) == ommSrcHashes.end()) {
          ommSrcHashes.push_back(ommRequest.ommSrcHash);
          ommRequests.emplace_back(std::move(ommRequest));
        }
      }

      ommSrcHash = XXH64(ommSrcHashes.data(), ommSrcHashes.size() * sizeof(ommSrcHashes[0]), kEmptyHash);

    } else {
      ommRequests.emplace_back(instance, instanceManager, m_settings);
      ommSrcHash = ommRequests[0].ommSrcHash;
    }

    updateSourceHash(instance, ommSrcHash);
  }

  bool OpacityMicromapManager::registerOpacityMicromapBuildRequest(RtInstance& instance,
                                                                   const InstanceManager& instanceManager,
                                                                   const std::vector<TextureRef>& textures) {
    if (!doesInstanceUseOpacityMicromap(instance))
      return false;

    if (!areInstanceTexturesResident(instance, textures))
      return false;
    
    InstanceOmmRequests ommRequests;

    generateInstanceOmmRequests(instance, instanceManager, ommRequests.ommRequests);

    // Bookkeep the requests now so that they can be released should any registers fail below
    m_instanceOmmRequests.emplace(instance.getOpacityMicromapSourceHash(), ommRequests);

    bool allRegistersSucceeded = true;

    // Register all omm requests for the instance
    for (auto& ommRequest : ommRequests.ommRequests)
      allRegistersSucceeded &= registerOmmRequestInternal(instance, ommRequest);

    // Purge the instance omm requests that didn't end up with any active omm requests
    // ToDo: should avoid adding into the list in the first place as this happens for ommRequests that
    //   have already been completed as well
    auto instanceOmmRequestsIter = m_instanceOmmRequests.find(instance.getOpacityMicromapSourceHash());
    if (instanceOmmRequestsIter->second.numActiveRequests == 0)
      m_instanceOmmRequests.erase(instanceOmmRequestsIter);
      
    return allRegistersSucceeded;
  }

  bool OpacityMicromapManager::registerOmmRequestInternal(RtInstance& instance, const OmmRequest& ommRequest) {

    XXH64_hash_t ommSrcHash = ommRequest.ommSrcHash;

    if (ommSrcHash == kEmptyHash) {
      ONCE(Logger::warn("[RTX Opacity Micromap] Build source instance has an invalid hash. Ignoring the build request."));
      return false;
    }

    auto ommCacheIterator = m_ommCache.find(ommSrcHash);

    // OMM request is not yet known
    if (ommCacheIterator == m_ommCache.end()) {
      return addNewOmmBuildRequest(instance, ommRequest);
    } else {

      auto& ommCacheItem = ommCacheIterator->second;

      // Check OMM request's parametrization matches that of the cached omm data
      // in case of an OMM hash collision
      if (!ommCacheItem.isCompatibleWithOmmRequest(ommRequest)) {
        ONCE(Logger::warn("[RTX Opacity Micromap] Found a cached Opacity Micromap with same hash but with incompatible parametrization. Black listing the Opacity Micromap hash."));
        m_blackListedList.insert(ommSrcHash);
        destroyOmmData(ommSrcHash);
        return false;
      }

      if (ommCacheItem.cacheState == OpacityMicromapCacheState::eStep1_Baking) {
        auto sourceDataIter = m_cachedSourceData.find(ommSrcHash);

        // Source data has been unlinked and removed from unprocessed list, add it back to the unprocessed list
        if (sourceDataIter == m_cachedSourceData.end()) {
          return insertToUnprocessedList(ommRequest);
        }
      }
    }

    return true;
  }

  XXH64_hash_t OpacityMicromapManager::tryBindOpacityMicromap(Rc<DxvkCommandList> cmdList,
                                                              const RtInstance& instance, uint32_t billboardIndex,
                                                              VkAccelerationStructureGeometryKHR& targetGeometry,
                                                              const InstanceManager& instanceManager) {
    if (!doesInstanceUseOpacityMicromap(instance))
      return kEmptyHash;

    return bindOpacityMicromap(cmdList, instance, billboardIndex, targetGeometry, instanceManager);
  }
  
  XXH64_hash_t OpacityMicromapManager::bindOpacityMicromap(Rc<DxvkCommandList> cmdList,
                                                           const RtInstance& instance, 
                                                           uint32_t billboardIndex,
                                                           VkAccelerationStructureGeometryKHR& targetGeometry,
                                                           const InstanceManager& instanceManager) {
    m_numRequestedOMMBindings++;

    if (!m_settings.enableBinding)
      return kEmptyHash;

    // ToDo: avoid fixing up the index here
    billboardIndex = 
      (instance.getBillboardCount() > 0 && m_settings.splitBillboardGeometry) ? billboardIndex : OmmRequest::kInvalidIndex;
    const OmmRequest ommRequest(instance, instanceManager, m_settings, billboardIndex);

    auto& ommCacheItemIter = m_ommCache.find(ommRequest.ommSrcHash);

    // OMM is not available in the cache
    if (ommCacheItemIter == m_ommCache.end())
      return kEmptyHash;

    bool boundOMM = false;
    OpacityMicromapCacheItem& ommCacheItem = ommCacheItemIter->second;
    const OpacityMicromapCacheState ommCacheState = ommCacheItem.cacheState;

    // Check OMM request's parametrization matches that of the cached omm data
    // in case of an OMM hash collision
    if (!ommCacheItem.isCompatibleWithOmmRequest(ommRequest)) {
      ONCE(Logger::warn("[RTX Opacity Micromap] Found a cached Opacity Microamp with a matching hash but with an incompatible parametrization. Discarding Opacity Micromap binding request."));
      return kEmptyHash;
    }

    ommCacheItem.lastUseFrameIndex = m_device->getCurrentFrameId();

    // Make the item most recently used
    m_leastRecentlyUsedList.splice(m_leastRecentlyUsedList.end(), m_leastRecentlyUsedList, ommCacheItem.leastRecentlyUsedListIter);

    // Bind OMM if the data is ready
    switch (ommCacheState) {
    case OpacityMicromapCacheState::eStep0_Unprocessed:
    case OpacityMicromapCacheState::eStep1_Baking:
    case OpacityMicromapCacheState::eStep2_Baked:
      // OMM data is not yet ready
      break;

    case OpacityMicromapCacheState::eStep3_Built:
    case OpacityMicromapCacheState::eStep4_Ready:
    {
      targetGeometry.geometry.triangles.pNext = &ommCacheItem.blasOmmBuffers->blasDesc;
      boundOMM = true;
      m_numBoundOMMs++;

      // Track the lifetime of the used buffers
      cmdList->trackResource<DxvkAccess::Read>(ommCacheItem.blasOmmBuffers);
      m_boundOMMs.push_back(ommCacheItem.blasOmmBuffers);
      break;
    }
    }

    if (ommCacheState == OpacityMicromapCacheState::eStep3_Built)
      m_boundOmmsRequireSynchronization = true;

    return boundOMM ? ommRequest.ommSrcHash : kEmptyHash;
  }

  void OpacityMicromapManager::addBarriersForBuiltOMMs(Rc<DxvkCommandList> cmdList) {

    if (m_boundOmmsRequireSynchronization) {

      // Add a barrier blocking on OMM builds
      {
        VkMemoryBarrier2 memoryBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, NULL,
          VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT, VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT,
          VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_MICROMAP_READ_BIT_EXT };
        VkDependencyInfo dependencyInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };

        dependencyInfo.memoryBarrierCount = 1;
        dependencyInfo.pMemoryBarriers = &memoryBarrier;

        cmdList->vkCmdPipelineBarrier2KHR(&dependencyInfo);
      }

      // All built instances have been synchronized, move them to the ready list
      {
        for (auto& ommSrcHash : m_builtList) {
          m_ommCache[ommSrcHash].cacheState = OpacityMicromapCacheState::eStep4_Ready;
        }
        m_readyList.splice(m_readyList.end(), m_builtList);
      }

      m_boundOmmsRequireSynchronization = false;
    }
  }

  template <typename IndexType>
  void calculateMicromapTriangleArrayBufferSizes(uint32_t numAllocatedTriangles,
                                                 uint32_t& triangleArrayBufferSize,
                                                 uint32_t& triangleIndexBufferSize) {
    triangleArrayBufferSize = numAllocatedTriangles * sizeof(VkMicromapTriangleEXT);
    triangleIndexBufferSize = numAllocatedTriangles * sizeof(IndexType);
  };

  template <typename IndexType>
  OpacityMicromapManager::OmmResult initializeOpacityMicromapTriangleArrayBuffers(
    Rc<DxvkDevice> device,
    Rc<DxvkContext> ctx,
    Rc<DxvkCommandList> cmdList,
    VkOpacityMicromapFormatEXT ommFormat,
    uint16_t subdivisionLevel,
    uint32_t numTriangles,
    uint32_t opacityMicromapPerTriangleBufferSize,
    Rc<DxvkBuffer>& triangleArrayBuffer,
    Rc<DxvkBuffer>& triangleIndexBuffer) {

    uint32_t triangleArrayBufferSize;
    uint32_t triangleIndexBufferSize;
    calculateMicromapTriangleArrayBufferSizes<IndexType>(numTriangles, triangleArrayBufferSize, triangleIndexBufferSize);

    // Create buffers
    {
      DxvkBufferCreateInfo ommBufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
      ommBufferInfo.usage = VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT | 
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      ommBufferInfo.access = VK_ACCESS_TRANSFER_WRITE_BIT;

      ommBufferInfo.size = triangleArrayBufferSize;
      triangleArrayBuffer = device->createBuffer(ommBufferInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXOpacityMicromap);
      
      if (triangleArrayBuffer == nullptr) {
        ONCE(Logger::warn(str::format("[RTX - Opacity Micromap] Failed to allocate triangle buffers due to m_device->createBuffer() failing to allocate a buffer for size: ", ommBufferInfo.size)));
        return OpacityMicromapManager::OmmResult::OutOfMemory;
      }
      
      ommBufferInfo.size = triangleIndexBufferSize;
      triangleIndexBuffer = device->createBuffer(ommBufferInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXOpacityMicromap);

      if (triangleIndexBuffer == nullptr) {
        ONCE(Logger::warn(str::format("[RTX - Opacity Micromap] Failed to allocate triangle buffers due to m_device->createBuffer() failing to allocate a buffer for size: ", ommBufferInfo.size)));
        return OpacityMicromapManager::OmmResult::OutOfMemory;
      }
    }

    // Micromap triangle buffer desc
    VkMicromapTriangleEXT micromapTriangleDescTemplate;
    micromapTriangleDescTemplate.dataOffset = 0;           // Offset in opacityMicromapBuffer
    micromapTriangleDescTemplate.format = ommFormat;
    micromapTriangleDescTemplate.subdivisionLevel = subdivisionLevel;

    std::vector<VkMicromapTriangleEXT> hostTriangleArrayBuffer(numTriangles);
    std::vector<IndexType> hostTriangleIndexBuffer(numTriangles);
    IndexType* ommIndex = hostTriangleIndexBuffer.data();

    for (IndexType i = 0; i < numTriangles; i++) {
      hostTriangleArrayBuffer[i] = micromapTriangleDescTemplate;
      hostTriangleArrayBuffer[i].dataOffset = i * opacityMicromapPerTriangleBufferSize;

      *(ommIndex++) = i;
    }

    DxvkBufferSliceHandle bufferSlice = triangleArrayBuffer->getSliceHandle(0, triangleArrayBufferSize);
    cmdList->cmdUpdateBuffer(
      DxvkCmdBuffer::ExecBuffer,
      bufferSlice.handle,
      bufferSlice.offset,
      bufferSlice.length,
      hostTriangleArrayBuffer.data());

    bufferSlice = triangleIndexBuffer->getSliceHandle(0, triangleIndexBufferSize);
    cmdList->cmdUpdateBuffer(
      DxvkCmdBuffer::ExecBuffer,
      bufferSlice.handle,
      bufferSlice.offset,
      bufferSlice.length,
      hostTriangleIndexBuffer.data());

    return OpacityMicromapManager::OmmResult::Success;
  }

  void OpacityMicromapManager::calculateMicromapBuildInfo(
    VkMicromapUsageEXT& ommUsageGroup,
    VkMicromapBuildInfoEXT& ommBuildInfo,
    VkMicromapBuildSizesInfoEXT& sizeInfo) {
    ommBuildInfo = { VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT };
    sizeInfo = { VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT };

    // Get prebuild info
    ommBuildInfo.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
    ommBuildInfo.flags = 0;
    ommBuildInfo.mode = VK_BUILD_MICROMAP_MODE_BUILD_EXT;
    ommBuildInfo.dstMicromap = VK_NULL_HANDLE;
    ommBuildInfo.usageCountsCount = 1;
    ommBuildInfo.pUsageCounts = &ommUsageGroup;
    ommBuildInfo.data.deviceAddress = 0ull;
    ommBuildInfo.triangleArray.deviceAddress = 0ull;
    ommBuildInfo.triangleArrayStride = 0;

    m_device->vkd()->vkGetMicromapBuildSizesEXT(m_device->vkd()->device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &ommBuildInfo, &sizeInfo);
  }

  void OpacityMicromapManager::calculateRequiredVRamSize(
    uint32_t numTriangles,
    uint16_t subdivisionLevel,
    VkOpacityMicromapFormatEXT ommFormat,
    VkIndexType triangleIndexType,
    VkDeviceSize& arrayBufferDeviceSize,
    VkDeviceSize& blasOmmBuffersDeviceSize) {
    const uint32_t numMicroTrianglesPerTriangle = calculateNumMicroTriangles(subdivisionLevel);
    const uint32_t numMicroTriangles = numTriangles * numMicroTrianglesPerTriangle;
    const uint8_t numOpacityMicromapBitsPerMicroTriangle = ommFormat == VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT ? 1 : 2;
    const uint32_t opacityMicromapPerTriangleBufferSize = dxvk::util::ceilDivide(numMicroTrianglesPerTriangle * numOpacityMicromapBitsPerMicroTriangle, 8);
    const uint32_t opacityMicromapBufferSize = numTriangles * opacityMicromapPerTriangleBufferSize;

    // Account for any alignments at start and the end of buffers
    arrayBufferDeviceSize = opacityMicromapBufferSize + 2 * kOmmBufferAlignment;

    // Fill out VkMicromapUsageEXT with size information
    // For now all triangles are in the same micromap group
    VkMicromapUsageEXT ommUsageGroup = {};
    ommUsageGroup.count = numTriangles;
    ommUsageGroup.subdivisionLevel = subdivisionLevel;
    ommUsageGroup.format = ommFormat;

    // Get micromap prebuild info
    VkMicromapBuildInfoEXT ommBuildInfo = { VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT };
    VkMicromapBuildSizesInfoEXT sizeInfo = { VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT };
    calculateMicromapBuildInfo(ommUsageGroup, ommBuildInfo, sizeInfo);

    uint32_t triangleArrayBufferSize;
    uint32_t triangleIndexBufferSize;
    if (triangleIndexType == VK_INDEX_TYPE_UINT16)
      calculateMicromapTriangleArrayBufferSizes<uint16_t>(numTriangles, triangleArrayBufferSize, triangleIndexBufferSize);
    else
      calculateMicromapTriangleArrayBufferSizes<uint32_t>(numTriangles, triangleArrayBufferSize, triangleIndexBufferSize);

    // Account for any alignments at start and the end of buffers
    blasOmmBuffersDeviceSize =
      triangleArrayBufferSize + 2 * kOmmBufferAlignment +
      triangleIndexBufferSize + 2 * kOmmBufferAlignment +
      sizeInfo.micromapSize + 2 * kMicromapBufferAlignment;
  }
  
  OpacityMicromapManager::OmmResult OpacityMicromapManager::bakeOpacityMicromapArray(
    Rc<DxvkContext> ctx,
    Rc<DxvkCommandList> cmdList,
    XXH64_hash_t ommSrcHash,
    OpacityMicromapCacheItem& ommCacheItem,
    CachedSourceData& sourceData,
    const std::vector<TextureRef>& textures,
    uint32_t& maxMicroTrianglesToBake) {
    
    const RtInstance& instance = *sourceData.getInstance();
    
    if ((instance.getMaterialType() != RtSurfaceMaterialType::Opaque &&
         instance.getMaterialType() != RtSurfaceMaterialType::RayPortal)) {
      ONCE(Logger::warn("[RTX Opacity Micromap] Unsupported material type. Opacity lookup for the material type is not supported in the Opacity Micromap baker. Ignoring the bake request."));
      return OmmResult::Failure;
    }

    if (!areInstanceTexturesResident(instance, textures)) {
      return OmmResult::DependenciesUnavailable;
    }

    BlasEntry& blasEntry = *instance.getBlas();

    const uint32_t numTriangles = sourceData.numTriangles;
    const uint32_t numMicroTrianglesPerTriangle = calculateNumMicroTriangles(ommCacheItem.subdivisionLevel);
    const uint32_t numMicroTriangles = numTriangles * numMicroTrianglesPerTriangle;
    const uint8_t numOpacityMicromapBitsPerMicroTriangle = ommCacheItem.ommFormat == VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT ? 1 : 2;
    const uint32_t opacityMicromapPerTriangleBufferSize = dxvk::util::ceilDivide(numMicroTrianglesPerTriangle * numOpacityMicromapBitsPerMicroTriangle, 8);
    const uint32_t opacityMicromapBufferSize = numTriangles * opacityMicromapPerTriangleBufferSize;

    // Preallocate all the device memory needed to build the OMM item
    if (ommCacheItem.getDeviceSize() == 0)
    {
      VkDeviceSize arrayBufferDeviceSize;
      VkDeviceSize blasOmmBuffersDeviceSize;

      const VkIndexType triangleIndexType = numTriangles <= UINT16_MAX ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
      calculateRequiredVRamSize(numTriangles, ommCacheItem.subdivisionLevel, ommCacheItem.ommFormat, triangleIndexType,
                                arrayBufferDeviceSize, blasOmmBuffersDeviceSize);

      VkDeviceSize requiredDeviceSize = arrayBufferDeviceSize + blasOmmBuffersDeviceSize;

      if (!m_memoryManager.allocate(requiredDeviceSize)) {
        m_amountOfMemoryMissing += requiredDeviceSize;
        return OmmResult::OutOfMemory;
      }

      ommCacheItem.arrayBufferDeviceSize = arrayBufferDeviceSize;
      ommCacheItem.blasOmmBuffersDeviceSize = blasOmmBuffersDeviceSize;
    }

    // Create micromap buffer
    if (!ommCacheItem.ommArrayBuffer.ptr())
    {
      DxvkBufferCreateInfo ommBufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
      ommBufferInfo.usage = VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
      ommBufferInfo.access = VK_ACCESS_SHADER_WRITE_BIT;
      ommBufferInfo.size = opacityMicromapBufferSize;
      ommCacheItem.ommArrayBuffer = m_device->createBuffer(ommBufferInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXOpacityMicromap);

      if (ommCacheItem.ommArrayBuffer == nullptr) {
        ONCE(Logger::warn(str::format("[RTX - Opacity Micromap] Failed to allocate OMM array buffer due to m_device->createBuffer() failing to allocate a buffer for size: ", ommBufferInfo.size)));
        return OmmResult::OutOfMemory;
      }
    }

    // Generate OMM array
    {
      const TextureRef opacityTexture = textures[instance.getAlbedoOpacityTextureIndex()];
      const TextureRef* secondaryOpacityTexture = nullptr;
      if (instance.getSecondaryOpacityTextureIndex() != kSurfaceMaterialInvalidTextureIndex)
        secondaryOpacityTexture = &textures[instance.getSecondaryOpacityTextureIndex()];

      RtxGeometryUtils::BakeOpacityMicromapDesc desc = {};
      desc.subdivisionLevel = ommCacheItem.subdivisionLevel;
      desc.numMicroTrianglesPerTriangle = calculateNumMicroTriangles(ommCacheItem.subdivisionLevel);
      desc.ommFormat = ommCacheItem.ommFormat;
      desc.surfaceIndex = instance.getSurfaceIndex();
      desc.materialType = instance.getMaterialType();
      desc.applyVertexAndTextureOperations = ommCacheItem.useVertexAndTextureOperations;
      desc.useConservativeEstimation = m_opacityMicromapUseConservativeEstimation;
      desc.conservativeEstimationMaxTexelTapsPerMicroTriangle = m_opacityMicromapConservativeEstimationMaxTexelTapsPerMicroTriangle;
      desc.maxNumMicroTrianglesToBake = maxMicroTrianglesToBake;
      desc.numTriangles = numTriangles;
      desc.triangleOffset = sourceData.triangleOffset;
      desc.resolveTransparencyThreshold = RtxOptions::Get()->getResolveTransparencyThreshold();
      desc.resolveOpaquenessThreshold = RtxOptions::Get()->getResolveOpaquenessThreshold();

      // Overrides
      if (instance.surface.alphaState.isDecal)
        desc.resolveTransparencyThreshold = std::max(desc.resolveTransparencyThreshold, m_settings.decalsMinResolveTransparencyThreshold);

      ctx->getCommonObjects()->metaGeometryUtils().dispatchBakeOpacityMicromap(
        m_device, cmdList, ctx, blasEntry.modifiedGeometryData, opacityTexture, secondaryOpacityTexture, 
        desc, ommCacheItem.bakingState, ommCacheItem.ommArrayBuffer);

      cmdList->trackResource<DxvkAccess::Write>(ommCacheItem.ommArrayBuffer);

      maxMicroTrianglesToBake -= std::min(ommCacheItem.bakingState.numMicroTrianglesBakedInLastBake, maxMicroTrianglesToBake);
    }

    m_numMicroTrianglesBaked += ommCacheItem.bakingState.numMicroTrianglesBakedInLastBake;

    return OmmResult::Success;
  }

  OpacityMicromapManager::OmmResult OpacityMicromapManager::buildOpacityMicromap(
    Rc<DxvkContext> ctx,
    Rc<DxvkCommandList> cmdList,
    XXH64_hash_t ommSrcHash,
    OpacityMicromapCacheItem& ommCacheItem,
    VkMicromapUsageEXT& ommUsageGroup,
    VkMicromapBuildInfoEXT& ommBuildInfo,
    uint32_t& maxMicroTrianglesToBuild,
    bool forceBuild) {
    
    auto sourceDataIter = m_cachedSourceData.find(ommSrcHash);
    omm_validation_assert(sourceDataIter != m_cachedSourceData.end());
    CachedSourceData& sourceData = sourceDataIter->second;

    const uint32_t numMicroTrianglesPerTriangle = calculateNumMicroTriangles(ommCacheItem.subdivisionLevel);
    const uint32_t numTriangles = sourceData.numTriangles;
    const uint32_t numMicroTriangles = numTriangles * numMicroTrianglesPerTriangle;

    // OMM builds are at per OMM item granularity
    if (!forceBuild && numMicroTriangles > maxMicroTrianglesToBuild)
      return OmmResult::OutOfBudget;

    const uint8_t numOpacityMicromapBitsPerMicroTriangle = ommCacheItem.ommFormat == VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT ? 1 : 2;
    const uint32_t opacityMicromapPerTriangleBufferSize = dxvk::util::ceilDivide(numMicroTrianglesPerTriangle * numOpacityMicromapBitsPerMicroTriangle, 8);
    const uint32_t opacityMicromapBufferSize = numTriangles * opacityMicromapPerTriangleBufferSize;
    const VkIndexType triangleIndexType = numTriangles <= UINT16_MAX ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    const uint32_t numBytesPerIndexElement = triangleIndexType == VK_INDEX_TYPE_UINT16 ? 2 : 4;
    ommCacheItem.blasOmmBuffers = new DxvkOpacityMicromap(m_device);

    // Micromap forward definitions
    Rc<DxvkBuffer> triangleArrayBuffer;     // VkMicromapTriangleEXT per triangle
    
    // Fill out VkMicromapUsageEXT with size information
    // For now all triangles are in the same micromap group
    ommUsageGroup = {};
    ommUsageGroup.count = numTriangles;
    ommUsageGroup.subdivisionLevel = ommCacheItem.subdivisionLevel;
    ommUsageGroup.format = ommCacheItem.ommFormat;

    // Get micromap prebuild info
    ommBuildInfo = {};
    ommBuildInfo.sType = VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT;
    VkMicromapBuildSizesInfoEXT sizeInfo = { VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT };
    calculateMicromapBuildInfo(ommUsageGroup, ommBuildInfo, sizeInfo);

    // Initialize micromap triangle index buffers
    {
      OmmResult result;
      if (triangleIndexType == VK_INDEX_TYPE_UINT16)
        result = initializeOpacityMicromapTriangleArrayBuffers<uint16_t>(
          m_device, ctx, cmdList, ommCacheItem.ommFormat, ommCacheItem.subdivisionLevel, numTriangles, opacityMicromapPerTriangleBufferSize,
          triangleArrayBuffer, ommCacheItem.blasOmmBuffers->opacityMicromapTriangleIndexBuffer);
      else
        result = initializeOpacityMicromapTriangleArrayBuffers<uint32_t>(
          m_device, ctx, cmdList, ommCacheItem.ommFormat, ommCacheItem.subdivisionLevel, numTriangles, opacityMicromapPerTriangleBufferSize,
          triangleArrayBuffer, ommCacheItem.blasOmmBuffers->opacityMicromapTriangleIndexBuffer);

      if (result != OmmResult::Success)
        return result;
    }

    // Create micromap
    {
      // Create buffer
      DxvkBufferCreateInfo ommBufferInfo = { VK_STRUCTURE_TYPE_MICROMAP_CREATE_INFO_EXT };
      ommBufferInfo.usage = VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT;
      // ToDo: revisit. Access should be VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT, but the EXT flag is not compatible here
      // The access is covered by a proper VkMemoryBarrier2 later
      ommBufferInfo.access = VK_ACCESS_MEMORY_WRITE_BIT;
      ommBufferInfo.size = sizeInfo.micromapSize;
      ommCacheItem.blasOmmBuffers->opacityMicromapBuffer = m_device->createBuffer(ommBufferInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXOpacityMicromap);

      if (ommCacheItem.blasOmmBuffers->opacityMicromapBuffer == nullptr) {
        ONCE(Logger::warn(str::format("[RTX - Opacity Micromap] Failed to build a micromap due to m_device->createBuffer() failing to allocate a buffer for size: ", ommBufferInfo.size)));
        return OmmResult::OutOfMemory;
      }

      // Create micromap
      VkMicromapCreateInfoEXT maCreateInfo = { VK_STRUCTURE_TYPE_MICROMAP_CREATE_INFO_EXT };
      maCreateInfo.createFlags = 0;
      maCreateInfo.buffer = ommCacheItem.blasOmmBuffers->opacityMicromapBuffer->getBufferRaw();
      maCreateInfo.offset = 0;
      maCreateInfo.size = sizeInfo.micromapSize;
      maCreateInfo.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
      maCreateInfo.deviceAddress = 0ull;

      if (vkFailed(m_device->vkd()->vkCreateMicromapEXT(m_device->vkd()->device(), &maCreateInfo, nullptr, &ommCacheItem.blasOmmBuffers->opacityMicromap))) {
        ONCE(Logger::warn("[RTX Opacity Micromap] Failed to build a micromap. Ignoring the build request."));
        return OmmResult::Failure;
      }
    }
    
    // Allocate the scratch memory
    uint32_t scratchAlignment = m_device->properties().khrDeviceAccelerationStructureProperties.minAccelerationStructureScratchOffsetAlignment;
    DxvkBufferSlice scratchSlice = m_scratchAllocator.alloc(scratchAlignment, sizeInfo.buildScratchSize);

    // Build the array with vkBuildMicromapsEXT
    {
      VkBufferDeviceAddressInfo ommBufferAddressInfo = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, NULL, ommCacheItem.ommArrayBuffer->getBufferRaw() };
      VkDeviceAddress ommBufferAddress = vkGetBufferDeviceAddress(m_device->vkd()->device(), &ommBufferAddressInfo);

      VkBufferDeviceAddressInfo ommTriangleArrayBufferInfo = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, NULL, triangleArrayBuffer->getBufferRaw() };
      VkDeviceAddress ommTriangleArrayBufferAddress = vkGetBufferDeviceAddress(m_device->vkd()->device(), &ommTriangleArrayBufferInfo);
      
      // Fill in the pointers we didn't have at size query
      ommBuildInfo.dstMicromap = ommCacheItem.blasOmmBuffers->opacityMicromap;
      ommBuildInfo.data.deviceAddress = ommBufferAddress;
      ommBuildInfo.triangleArray.deviceAddress = ommTriangleArrayBufferAddress;
      ommBuildInfo.scratchData.deviceAddress = scratchSlice.getDeviceAddress();
      ommBuildInfo.triangleArrayStride = sizeof(VkMicromapTriangleEXT);
      
      cmdList->trackResource<DxvkAccess::Read>(ommCacheItem.ommArrayBuffer);
      cmdList->trackResource<DxvkAccess::Read>(triangleArrayBuffer);
      cmdList->trackResource<DxvkAccess::Write>(scratchSlice.buffer());

      // Release OMM array memory as it's no longer needed after the build
      {
        m_memoryManager.release(ommCacheItem.arrayBufferDeviceSize);
        ommCacheItem.arrayBufferDeviceSize = 0;
        ommCacheItem.ommArrayBuffer = nullptr;
      }
    }

    // Update the BLAS desc with the built micromap
    {
      VkBufferDeviceAddressInfo ommTriangleIndexBufferInfo = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, NULL, ommCacheItem.blasOmmBuffers->opacityMicromapTriangleIndexBuffer->getBufferRaw() };
      VkDeviceAddress ommTriangleIndexBufferAddress = vkGetBufferDeviceAddress(m_device->vkd()->device(), &ommTriangleIndexBufferInfo);

      VkAccelerationStructureTrianglesOpacityMicromapEXT& ommBlasDesc = ommCacheItem.blasOmmBuffers->blasDesc;
      ommBlasDesc = VkAccelerationStructureTrianglesOpacityMicromapEXT { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_TRIANGLES_OPACITY_MICROMAP_EXT };
      ommBlasDesc.micromap = ommCacheItem.blasOmmBuffers->opacityMicromap;
      ommBlasDesc.indexType = triangleIndexType;
      ommBlasDesc.indexBuffer.deviceAddress = ommTriangleIndexBufferAddress;
      ommBlasDesc.indexStride = numBytesPerIndexElement;
      ommBlasDesc.baseTriangle = 0;
    }
  
    // Track the lifetime of all the build buffers needed for BLAS, including non-ref counted .opacityMicromap
    cmdList->trackResource<DxvkAccess::Write>(ommCacheItem.blasOmmBuffers);

    m_numMicroTrianglesBuilt += numMicroTriangles;
    maxMicroTrianglesToBuild -= std::min(numMicroTriangles, maxMicroTrianglesToBuild);

    // Source data is no longer needed
    deleteCachedSourceData(sourceDataIter, ommCacheItem.cacheState, true);

#ifdef VALIDATION_MODE
    Logger::warn(str::format("[RTX Opacity Micromap] m_cachedSourceData.erase(", ommSrcHash, ") by thread_id ", std::this_thread::get_id()));
#endif
    
    return OmmResult::Success;
  }

  void OpacityMicromapManager::bakeOpacityMicromapArrays(Rc<DxvkContext> ctx,
                                                         Rc<DxvkCommandList> cmdList,
                                                         const std::vector<TextureRef>& textures,
                                                         uint32_t& maxMicroTrianglesToBake) {

    if (!m_settings.enableBakingArrays)
      return;

#ifdef VALIDATION_MODE
    for (auto iter0 = m_unprocessedList.begin(); iter0 != m_unprocessedList.end(); iter0++) {
      auto iter1 = iter0;
      iter1++;
      for (; iter1 != m_unprocessedList.end(); iter1++) {
        if (*iter1 == *iter0) {
          omm_validation_assert(0 && "Duplicate entries found in a list");
        }
      }
    }
    for (auto iter0 = m_cachedSourceData.begin(); iter0 != m_cachedSourceData.end(); iter0++) {
      OpacityMicromapCacheItem& ommCacheItem = m_ommCache[iter0->first];
      if ((ommCacheItem.cacheState <= OpacityMicromapCacheState::eStep0_Unprocessed) &&
           iter0->second.getInstance() == nullptr)
        omm_validation_assert(0 && "Instance is null at unexpected stage");
    }
#endif

    ScopedGpuProfileZone(ctx, "Bake Opacity Micromap Arrays");

    for (auto ommSrcHashIter = m_unprocessedList.begin(); ommSrcHashIter != m_unprocessedList.end() && maxMicroTrianglesToBake > 0; ) {
      XXH64_hash_t ommSrcHash = *ommSrcHashIter;

#ifdef VALIDATION_MODE
      Logger::warn(str::format("[RTX Opacity Micromap] Baking ", ommSrcHash, " on thread_id ", std::this_thread::get_id()));
#endif

      auto sourceDataIter = m_cachedSourceData.find(ommSrcHash);
      auto cacheItemIter = m_ommCache.find(ommSrcHash);

      if (sourceDataIter == m_cachedSourceData.end() || cacheItemIter == m_ommCache.end()) {
        assert(0 && "OMM inconsistent state");
        ONCE(Logger::err("[RTX Opacity Micromap] Encountered inconsistent state. Opacity Micromap item listed for baking is missing required state data. Skipping it."));
        // First update the iterator, then destroy any omm data associated with it
        ommSrcHashIter++;
        destroyOmmData(ommSrcHash);
        continue;
      }

      CachedSourceData& sourceData = sourceDataIter->second;
      OpacityMicromapCacheItem& ommCacheItem = cacheItemIter->second;
      ommCacheItem.cacheState = OpacityMicromapCacheState::eStep1_Baking;

      OmmResult result = bakeOpacityMicromapArray(ctx, cmdList, ommSrcHash, ommCacheItem, sourceData, textures, maxMicroTrianglesToBake);

      if (result == OmmResult::Success) {
        // Use >= as the number of baked micro triangles is aligned up
        if (ommCacheItem.bakingState.numMicroTrianglesBaked >= ommCacheItem.bakingState.numMicroTrianglesToBake) {
          // Unlink the referenced RtInstance
          sourceData.setInstance(nullptr, m_instanceOmmRequests);

          // First update the iterator, then pop the built element
          ommCacheItem.cacheState = OpacityMicromapCacheState::eStep2_Baked;
          m_bakedList.push_back(ommSrcHash);
          auto ommSrcHashIterToErase = ommSrcHashIter++;
          m_unprocessedList.erase(ommSrcHashIterToErase);
        }
        else {
        // Do nothing, else path means all the budget has been used up and thus the loop will exit due to maxMicroTrianglesToBake == 0
        //   so don't need to increment the iterator
        }
      } else if (result == OmmResult::OutOfMemory) {
        // Do nothing, try the next one
        ommSrcHashIter++;
        ONCE(Logger::debug("[RTX Opacity Micromap] Baking Opacity Micromap Array failed as ran out of memory."));
      } else if (result == OmmResult::DependenciesUnavailable) {
        // Textures not available - try the next one
        ommSrcHashIter++;
      } else if (result == OmmResult::Failure) {
        ONCE(Logger::warn(str::format("[RTX Opacity Micromap] Baking Opacity Micromap Array failed for hash ", ommSrcHash, ". Ignoring and black listing the hash.")));
#ifdef VALIDATION_MODE
        Logger::warn(str::format("[RTX Opacity Micromap] Baking Opacity Micromap Array failed for hash ", ommSrcHash, ". Ignoring and black listing the hash."));
#endif
        // Baking failed, ditch the instance
        // First update the iterator, then remove the element
        const RtInstance* instanceToRemove = sourceData.getInstance();
        ommSrcHashIter++;
        destroyInstance(*instanceToRemove, true);
        m_blackListedList.insert(ommSrcHash);
    } else { // OutOfBudget
      omm_validation_assert(0 && "Should not be hit");
      ommSrcHashIter++;
    }
#ifdef VALIDATION_MODE
      Logger::warn(str::format("[RTX Opacity Micromap] ~Baking ", ommSrcHash, " on thread_id ", std::this_thread::get_id()));
#endif
    }
  }

  void OpacityMicromapManager::buildOpacityMicromapsInternal(Rc<DxvkContext> ctx,
                                                             Rc<DxvkCommandList> cmdList,
                                                             uint32_t& maxMicroTrianglesToBuild) {

    if (!m_settings.enableBuilding)
      return;

#ifdef VALIDATION_MODE
    for (auto iter0 = m_bakedList.begin(); iter0 != m_bakedList.end(); iter0++) {
      auto iter1 = iter0;
      iter1++;
      for (; iter1 != m_bakedList.end(); iter1++) {
        if (*iter1 == *iter0) {
          omm_validation_assert(0 && "Duplicate entries found in a list");
        }
      }
      for (auto iter2 = m_unprocessedList.begin(); iter2 != m_unprocessedList.end(); iter2++) {
        if (*iter2 == *iter0) {
          omm_validation_assert(0 && "Two lists contain same OMM src hash");
        }
      }
    }
#endif

    ScopedGpuProfileZone(ctx, "Build Opacity Micromaps");

    // Pre-allocate the arrays because build infos include pointers to usage groups,
    // and reallocating vectors would invalidate these pointers
    const uint32_t maxBuildItems = m_bakedList.size();
    std::vector<VkMicromapUsageEXT> micromapUsageGroups(maxBuildItems);
    std::vector<VkMicromapBuildInfoEXT> micromapBuildInfos(maxBuildItems);
    uint32_t buildItemCount = 0;

    // Force at least one build since a build can't be split across frames even if doesn't fit within the budget
    // They're cheap regardless, so it should be fine.
    bool forceOmmBuild = maxMicroTrianglesToBuild > 0;  

    for (auto ommSrcHashIter = m_bakedList.begin(); ommSrcHashIter != m_bakedList.end() && maxMicroTrianglesToBuild > 0; ) {
      XXH64_hash_t ommSrcHash = *ommSrcHashIter;
#ifdef VALIDATION_MODE
      Logger::warn(str::format("[RTX Opacity Micromap] Building ", ommSrcHash, " on thread_id ", std::this_thread::get_id()));
#endif
      auto& ommCacheItemIter = m_ommCache.find(ommSrcHash);
      OpacityMicromapCacheItem& ommCacheItem = ommCacheItemIter->second;

      OmmResult result = buildOpacityMicromap(ctx, cmdList, *ommSrcHashIter, ommCacheItem, micromapUsageGroups[buildItemCount],
                                              micromapBuildInfos[buildItemCount], maxMicroTrianglesToBuild, forceOmmBuild);
      
      if (result == OmmResult::Success) {
        // First update the iterator, then pop the built element
        ommCacheItem.cacheState = OpacityMicromapCacheState::eStep3_Built;
        m_builtList.push_back(ommSrcHash);
        auto ommSrcHashIterToErase = ommSrcHashIter++;
        m_bakedList.erase(ommSrcHashIterToErase);
        ++buildItemCount;

        forceOmmBuild = false;
      }
      else if (result == OmmResult::Failure) {
#ifdef VALIDATION_MODE
        ONCE(Logger::warn(str::format("[RTX Opacity Micromap] Building Opacity Micromap failed for hash ", ommSrcHash, ".Ignoring and black listing the hash.")));
#endif
        // Building failed, ditch the OMM data
        // First update the iterator, then remove the element
        ommSrcHashIter++;
        destroyOmmData(ommCacheItemIter);
        m_blackListedList.insert(ommSrcHash);
      } else if (result == OmmResult::OutOfBudget) {
        // Do nothing, continue onto the next
        ommSrcHashIter++;
      } else if (result == OmmResult::OutOfMemory) {
        // Do nothing, try the next one
        ommSrcHashIter++;
        ONCE(Logger::warn("[RTX Opacity Micromap] Building Opacity Micromap Array failed as it ran out of memory."));
      } else {
        omm_validation_assert(0 && "Should not be hit");
        ommSrcHashIter++;
      }
#ifdef VALIDATION_MODE
      Logger::warn(str::format("[RTX Opacity Micromap] ~Building ", ommSrcHash, " on thread_id ", std::this_thread::get_id()));
#endif
    }

    if (buildItemCount > 0) {
      // Add a barrier needed for Micromap build reading the triangleArrayBuffer's and triangleIndexBuffer's
      {
        VkMemoryBarrier2 memoryBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, nullptr,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
          VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT, VK_ACCESS_SHADER_READ_BIT };
        VkDependencyInfo dependencyInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };

        dependencyInfo.memoryBarrierCount = 1;
        dependencyInfo.pMemoryBarriers = &memoryBarrier;

        cmdList->vkCmdPipelineBarrier2KHR(&dependencyInfo);
      }

      // Build the micromaps
      cmdList->vkCmdBuildMicromapsEXT(buildItemCount, micromapBuildInfos.data());
    }
  }

  void OpacityMicromapManager::onFrameStart(Rc<DxvkContext> ctx, Rc<DxvkCommandList> cmdList) {
    ScopedCpuProfileZone();
    const uint32_t currentFrameIndex = m_device->getCurrentFrameId();

    m_numBoundOMMs = 0;
    m_numRequestedOMMBindings = 0;

    bool forceRebuildOMMs =
      m_opacityMicromapUseConservativeEstimation != m_settings.enableConservativeEstimation ||
      m_opacityMicromapConservativeEstimationMaxTexelTapsPerMicroTriangle != m_settings.conservativeEstimationMaxTexelTapsPerMicroTriangle ||
      m_subdivisionLevel != m_settings.subdivisionLevel ||
      m_enableVertexAndTextureOperations != m_settings.enableVertexAndTextureOperations ||
      m_settings.enableResetEveryFrame;

    m_opacityMicromapUseConservativeEstimation = m_settings.enableConservativeEstimation;
    m_opacityMicromapConservativeEstimationMaxTexelTapsPerMicroTriangle = m_settings.conservativeEstimationMaxTexelTapsPerMicroTriangle;
    m_subdivisionLevel = m_settings.subdivisionLevel;
    m_enableVertexAndTextureOperations = m_settings.enableVertexAndTextureOperations;
    
    if (forceRebuildOMMs)
      clear();

    // Purge obsolete OMM Build Requests
    for (auto statIter = m_ommBuildRequestStatistics.begin(); statIter != m_ommBuildRequestStatistics.end();) {
      const uint32_t requestAge = currentFrameIndex - statIter->second.lastRequestFrameId;

      // Increment the iterator before any deletion
      auto currentStatIter = statIter++;

      if (requestAge > m_settings.ommBuildRequest_maxRequestFramesAge)
        m_ommBuildRequestStatistics.erase(currentStatIter);
    }
    
    // Account for OMM usage in BLASes in a previous TLAS
    // Tag the previously bound OMMs as used in this frame as well
    for (auto& previousFrameBoundOMM : m_boundOMMs)
      cmdList->trackResource<DxvkAccess::Read>(previousFrameBoundOMM);
    m_boundOMMs.clear();

    // Update memory management
    {
      const VkDeviceSize prevBudget = m_memoryManager.getBudget();
      m_memoryManager.updateMemoryBudget(ctx, m_settings);

      if (m_memoryManager.getBudget() != 0) {
        const bool hasVRamBudgetDecreased = m_memoryManager.getBudget() < prevBudget;

        // Adjust missing memory if the budget is oversubscribed
        if (hasVRamBudgetDecreased) {
          const VkDeviceSize used = m_memoryManager.getUsed();
          const VkDeviceSize budget = m_memoryManager.getBudget();

          m_amountOfMemoryMissing = std::max(m_amountOfMemoryMissing, std::max(used, budget) - budget);
        }

        // Adjust missing memory amount by an amount that's already pending to be made available
        m_amountOfMemoryMissing -= std::min(m_amountOfMemoryMissing, m_memoryManager.calculatePendingAvailableSize());

        // LRU cache eviction
        if (m_amountOfMemoryMissing > 0) {

          // Start evicting least recently used items 
          for (auto lruOmmSrcHashIter = m_leastRecentlyUsedList.begin();
               lruOmmSrcHashIter != m_leastRecentlyUsedList.end() && m_amountOfMemoryMissing > m_memoryManager.calculatePendingAvailableSize();
               ) {
            auto cacheItemIter = m_ommCache.find(*lruOmmSrcHashIter);
            if (cacheItemIter == m_ommCache.end()) {
              auto iterToDelete = lruOmmSrcHashIter;
              // Increment the iterator before any deletion
              lruOmmSrcHashIter++;
              ONCE(Logger::err("[RTX] Failed to find Opacity Micromap cache entry on LRU eviction"));
              m_leastRecentlyUsedList.erase(iterToDelete);
              continue;
            }

            const uint32_t cacheItemUsageFrameAge = currentFrameIndex - cacheItemIter->second.lastUseFrameIndex;

            // Stop eviction once an item is recent enough
            if (cacheItemUsageFrameAge < m_settings.minUsageFrameAgeBeforeEviction &&
              // Force eviction if the VRAM budget decreased to speed fitting into the budget up
              !hasVRamBudgetDecreased)
              break;

            // Increment the iterator before any deletion
            lruOmmSrcHashIter++;

            destroyOmmData(cacheItemIter);
          }
        }
      } else { // budget == 0
        if (prevBudget > 0)
          clear();
      }

      m_amountOfMemoryMissing = 0;

      // Call Memory Manager's onFrameStart last since any evicted buffers above 
      // were not used in this frame and thus should go to a pending release queue of the last frame
      m_memoryManager.onFrameStart();

      m_numMicroTrianglesBaked = 0;
      m_numMicroTrianglesBuilt = 0;
    }
  }

  void OpacityMicromapManager::buildOpacityMicromaps(Rc<DxvkContext> ctx,
                                                     Rc<DxvkCommandList> cmdList,
                                                     const std::vector<TextureRef>& textures,
                                                     uint32_t lastCameraCutFrameId,
                                                     float frameTimeSecs) {



    float workloadScalePerSecond = frameTimeSecs / (1 / 60.f);

    // Modulate the scale for practical FPS range (i.e. <25, 200>) to even out the OMM's per frame percentage performance overhead
    {
      // Scale set to balance evening out performance overhead across FPS as well as not to stray too 
      // far from linear scaling so as not to slow down baking at very high FPS too much
      // Stats for 4090 
      //
      // Scale = x
      // FPS | GPU Time | Frame Time %
      // 30  |  0.180   | 0.54
      // 74  |  0.143   | 1
      // 120 |  0.130   | 1.56
      // 200 |  0.110   | 2.2
      //
      // Scale = pow(x, 1.5f):
      // FPS | GPU Time | Frame Time %
      // 30  |  0.314   | 0.94
      // 74  |  0.145   | 1.01
      // 120 |  0.110   | 1.3
      // 200 |  0.079   | 1.6
      // 
      // Apply non-linear scaling only to an FPS range <25, 200> to avoid pow(t, 1.5f) blowing scaling out of proportion
      // Linear scaling will result in less overhead per frame for below 25 FPS, and in more overhead over 200 FPS
      if (frameTimeSecs >= 1 / 200.f && frameTimeSecs <= 1 / 25.f)
        workloadScalePerSecond = powf(workloadScalePerSecond, 1.5f);
      else if (frameTimeSecs > 1 / 25.f)
        workloadScalePerSecond *= 1.549f; // == non-linear scale multiplier at 25 FPS
      else
        workloadScalePerSecond *= 0.547f; // == non-linear scale multiplier at 200 FPS
    }

    const float secondToFrameBudgetScale = workloadScalePerSecond * frameTimeSecs;

    // Initialize per frame budgets
    float numMillionMicroTrianglesToBakeAvailable = m_settings.maxMicroTrianglesToBakeMillionPerSecond * secondToFrameBudgetScale;
    float numMillionMicroTrianglesToBuildAvailable = m_settings.maxMicroTrianglesToBuildMillionPerSecond * secondToFrameBudgetScale;

    if (m_device->getCurrentFrameId() - lastCameraCutFrameId < m_settings.numFramesAtStartToBuildWithHighWorkload) {
      numMillionMicroTrianglesToBakeAvailable *= m_settings.workloadHighWorkloadMultiplier;
      numMillionMicroTrianglesToBuildAvailable *= m_settings.workloadHighWorkloadMultiplier;
    }

    float fNumMicroTrianglesToBakeAvailable = numMillionMicroTrianglesToBakeAvailable * 1e6f;
    uint32_t numMicroTrianglesToBakeAvailable = fNumMicroTrianglesToBakeAvailable < UINT32_MAX ? static_cast<uint32_t>(fNumMicroTrianglesToBakeAvailable) : UINT32_MAX;
    float fNumMicroTrianglesToBuildAvailable = numMillionMicroTrianglesToBuildAvailable * 1e6f;
    uint32_t numMicroTrianglesToBuildAvailable = fNumMicroTrianglesToBuildAvailable < UINT32_MAX ? static_cast<uint32_t>(fNumMicroTrianglesToBuildAvailable) : UINT32_MAX;

    // Generate opacity micromaps
    if (!m_unprocessedList.empty() || !m_bakedList.empty()) {
      ScopedGpuProfileZone(ctx, "Process Opacity Micromaps");

      bakeOpacityMicromapArrays(ctx, cmdList, textures, numMicroTrianglesToBakeAvailable);
      buildOpacityMicromapsInternal(ctx, cmdList, numMicroTrianglesToBuildAvailable);
    }
  }
}  // namespace dxvk
