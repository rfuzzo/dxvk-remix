/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
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
#include "game_exporter.h"
#include "game_exporter_common.h"
#include "../util/log/log.h"

#include "usd_include_begin.h"
#include <pxr/usd/ar/defaultResolver.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/kind/registry.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/usd/tokens.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/primvar.h>
#include <pxr/usd/usdGeom/primvarsAPI.h> 
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include <pxr/usd/usdLux/lightapi.h>
#include <pxr/usd/usdLux/sphereLight.h> 
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/domeLight.h>
#include <pxr/usd/usdLux/shapingAPI.h>
#include <pxr/usd/usdRender/settings.h>
#include <pxr/usd/usdSkel/animation.h>
#include <pxr/usd/usdSkel/bindingAPI.h>
#include <pxr/usd/usdSkel/root.h>
#include <pxr/usd/usdSkel/skeleton.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/base/gf/camera.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/tf/fileUtils.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/plug/plugin.h>
#include "usd_include_end.h"

#include <algorithm>
#include <assert.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>

// Embedded MDLs
#include <AperturePBR_Opacity.mdl.h>
#include <AperturePBR_Translucent.mdl.h>
#include <AperturePBR_Model.mdl.h>
#include <AperturePBR_Normal.mdl.h>
#include "../util/util_env.h"

namespace {
inline pxr::GfMatrix4d ToRHS(const pxr::GfMatrix4d& xform) {
  static pxr::GfMatrix4d XYflip(pxr::GfVec4d(1.0, 1.0, -1.0, 1.0));

  // Change of Basis transform
  // X' = P * X * P-1
  return XYflip * xform * XYflip;
}

pxr::VtMatrix4dArray sanitizeBoneXforms(const pxr::VtMatrix4dArray& xforms,
                                        const pxr::VtMatrix4dArray& bindPose,
                                        const lss::Export::Meta& meta) {
  const size_t numBones = xforms.size();
  pxr::VtMatrix4dArray sanitizedXforms;
  sanitizedXforms.resize(numBones);
  pxr::GfMatrix4d worldFromRoot(1);


  if (numBones > 0) {
    const pxr::GfMatrix4d rootFromWorld = bindPose[0] * xforms[0];
    worldFromRoot = rootFromWorld.GetInverse();
    sanitizedXforms[0] = meta.isLHS ? ToRHS(rootFromWorld) : rootFromWorld;
  }
  for (int i = 1; i < numBones; ++i) {
    const pxr::GfMatrix4d xformFromRoot = bindPose[i] * xforms[i] * worldFromRoot;
    sanitizedXforms[i] = meta.isLHS ? ToRHS(xformFromRoot) : xformFromRoot;
  }

  return sanitizedXforms;
}

lss::Skeleton generateSkeleton(const size_t numBones,
                              const size_t bonesPerVertex,
                              const lss::PositionBuffer& points,
                              const lss::BlendWeightBuffer* weights,
                              const lss::BlendIndicesBuffer* indices) {
  lss::Skeleton output;
  output.bindPose.resize(numBones);
  output.restPose.resize(numBones);
  output.jointNames.resize(numBones);

  pxr::VtMatrix4dArray boneXforms;
  std::vector<pxr::GfVec3d> weightedPosSums;
  std::vector<float> totalWeights(numBones, 0);
  weightedPosSums.resize(numBones);
  const float equalBlend = 1.f / bonesPerVertex;

  if (numBones > 0) {
    for (int i = 0; i < points.size(); ++i) {
      for (int j = 0; j < bonesPerVertex; ++j) {
        const float weight = weights == nullptr ? equalBlend : (*weights)[i * bonesPerVertex + j];
        if (weight > 0.00001) {
          const int ind = indices == nullptr ? j : (*indices)[i * bonesPerVertex + j];
          weightedPosSums[ind] += points[i] * weight;
          totalWeights[ind] += weight;
        }
      }
    }
    //Note: Bind pose is global transforms, restPose is local transforms.

    pxr::GfVec3d rootBindPos(0);
    if (totalWeights[0] == 0) {
      output.bindPose[0].SetIdentity();
    } else {
      rootBindPos = weightedPosSums[0] / totalWeights[0];
      output.bindPose[0].SetTranslate(rootBindPos);
    }
    output.restPose[0] = output.bindPose[0];

    for (int i = 1; i < numBones; ++i) {
      if (totalWeights[i] == 0) {
        output.bindPose[i].SetIdentity();
        output.restPose[i].SetIdentity();
      } else {
        output.bindPose[i].SetTranslate(weightedPosSums[i] / totalWeights[i]);
        output.restPose[i].SetTranslate(weightedPosSums[i] / totalWeights[i] - rootBindPos);
      }
    }
  }
  output.jointNames[0] = pxr::TfToken("root");
  for (int i = 1; i < numBones; ++i) {
    output.jointNames[i] = pxr::TfToken(dxvk::str::format("root/joint", i));
  }
  return output;
}
}

namespace lss {

bool GameExporter::s_bMultiThreadSafety = false;
std::mutex GameExporter::s_mutex;

std::string computeLocalPath(const std::string& assetPath) {  
  static pxr::ArResolver& resolver = pxr::ArGetResolver();
  const std::string identifier = resolver.CreateIdentifierForNewAsset(assetPath);
  pxr::ArResolvedPath resolvedPath = resolver.ResolveForNewAsset(identifier);
  return resolvedPath.GetPathString();
}

bool GameExporter::loadUsdPlugins(const std::string& path) {
  static auto& pluginRegistry = pxr::PlugRegistry::GetInstance();
  std::string fullPath = computeLocalPath(path);
  static auto plugins = pluginRegistry.RegisterPlugins(fullPath);
  for (auto plugin : plugins) {
    if (plugin == nullptr)
      continue;

    if (!plugin->IsLoaded() && !plugin->Load()) {
      return false;
    }
    dxvk::Logger::info("[GameExporter] Load plugin: " + plugin->GetName());
  }
  return  plugins.size() > 0;
}

void GameExporter::exportUsd(const Export& exportData) {
  if(s_bMultiThreadSafety) {
    std::scoped_lock lock(s_mutex);
    exportUsdInternal(exportData);
  } else {
    exportUsdInternal(exportData);
  }
}

std::string getExtension(std::filesystem::path path) {
  return path.extension().generic_string();
}

void GameExporter::exportUsdInternal(const Export& exportData) {
  dxvk::Logger::info("[GameExporter][" + exportData.debugId + "] Export start");
  ExportContext ctx;
  lss::GameExporter::createApertureMdls(exportData.baseExportPath);
  ctx.instanceStage = (exportData.bExportInstanceStage) ? createInstanceStage(exportData) : pxr::UsdStageRefPtr();
  ctx.extension = (exportData.bExportInstanceStage) ? getExtension(exportData.instanceStagePath) : lss::ext::usd;
  exportMaterials(exportData, ctx);
  exportMeshes(exportData, ctx);
  exportSkeletons(exportData, ctx);
  if(ctx.instanceStage) {
    exportCamera(exportData, ctx);
    exportSphereLights(exportData, ctx);
    exportDistantLights(exportData, ctx);
    exportInstances(exportData, ctx);
    exportSky(exportData, ctx);
    setCommonStageMetaData(ctx.instanceStage, exportData);
    ctx.instanceStage->SetStartTimeCode(exportData.meta.startTimeCode);
    ctx.instanceStage->SetEndTimeCode(exportData.meta.endTimeCode);
    ctx.instanceStage->Save();
  }
  dxvk::Logger::info("[GameExporter][" + exportData.debugId + "] Export end");
}

pxr::UsdStageRefPtr GameExporter::createInstanceStage(const Export& exportData) {
  assert(exportData.bExportInstanceStage);
  pxr::UsdStageRefPtr instanceStage = pxr::UsdStage::CreateNew(exportData.instanceStagePath);
  assert(instanceStage);
  const auto rootPrim = instanceStage->DefinePrim(gRootNodePath);
  assert(rootPrim);
  instanceStage->SetDefaultPrim(rootPrim);
  const auto rootLightsPrim = instanceStage->DefinePrim(gRootLightsPath);
  assert(rootLightsPrim);
  const auto rootMeshesPrim = instanceStage->DefinePrim(gRootMeshesPath);
  assert(rootMeshesPrim);
  const auto rootMaterialsPrim = instanceStage->DefinePrim(gRootMaterialsPath);
  assert(rootMaterialsPrim);
  const auto rootInstancesPrim = instanceStage->DefinePrim(gRootInstancesPath);
  assert(rootInstancesPrim);
  dxvk::Logger::debug("[GameExporter][" + exportData.debugId + "] Creating instance stage");

  // capture meta data
  pxr::VtDictionary customLayerData;
  customLayerData.SetValueAtPath("lightspeed_layer_type", pxr::VtValue("capture"));
  customLayerData.SetValueAtPath("lightspeed_game_name", pxr::VtValue(exportData.meta.windowTitle));
  customLayerData.SetValueAtPath("lightspeed_exe_name", pxr::VtValue(exportData.meta.exeName));
  const auto relToCaptureIconPath = std::filesystem::relative(exportData.meta.iconPath, exportData.baseExportPath).string();
  customLayerData.SetValueAtPath("lightspeed_game_icon", pxr::VtValue(relToCaptureIconPath));
  customLayerData.SetValueAtPath("lightspeed_geometry_hash_rules", pxr::VtValue(exportData.meta.geometryHashRule));
  instanceStage->GetRootLayer()->SetCustomLayerData(customLayerData);

  return instanceStage;
}

void GameExporter::setCommonStageMetaData(pxr::UsdStageRefPtr stage, const Export& exportData) {
  stage->SetMetadata(pxr::TfToken("upAxis"), (exportData.meta.isZUp) ? pxr::TfToken("Z") : pxr::TfToken("Y"));
  stage->SetMetadata(pxr::TfToken("metersPerUnit"), exportData.meta.metersPerUnit);
  stage->SetTimeCodesPerSecond(exportData.meta.timeCodesPerSecond);

  // Write rendering settings to USD.
  if (exportData.meta.renderingSettingsDict.size() > 0) {
    const auto remixSettingsSdfPath = gStageRootPath.AppendChild(gTokRemixSettings);
    pxr::UsdRenderSettings settings = pxr::UsdRenderSettings::Define(stage, remixSettingsSdfPath);

    pxr::VtArray<std::string> configs;
    for (auto& pair : exportData.meta.renderingSettingsDict) {
      configs.push_back(pair.first + " = " + pair.second);
    }
    settings.GetPrim().CreateAttribute(pxr::TfToken("remix_config"), pxr::SdfValueTypeNames->StringArray).Set(configs);
  }
}

void GameExporter::createApertureMdls(const std::string& baseExportPath) {
  const std::string materialsDirPath = baseExportPath + "/" + commonDirName::matDir;
  dxvk::env::createDirectory(materialsDirPath);

  auto writeFile = [](const std::string& path, const auto& data) {
    std::ofstream stream(path, std::ios_base::binary);
    if (stream.is_open()) {
      stream.write(reinterpret_cast<const char*>(data), sizeof(data));
    } else {
      dxvk::Logger::info(dxvk::str::format("[GameExporter] Unable to create file: ", path));
    }
  };

  writeFile(materialsDirPath + "AperturePBR_Opacity.mdl", ___AperturePBR_Opacity);
  writeFile(materialsDirPath + "AperturePBR_Translucent.mdl", ___AperturePBR_Translucent);
  writeFile(materialsDirPath + "AperturePBR_Model.mdl", ___AperturePBR_Model);
  writeFile(materialsDirPath + "AperturePBR_Normal.mdl", ___AperturePBR_Normal);
}

void GameExporter::exportMaterials(const Export& exportData, ExportContext& ctx) {
  dxvk::Logger::debug("[GameExporter][" + exportData.debugId + "][exportMaterials] Begin");
  const std::string matDirPath = exportData.baseExportPath + "/" + commonDirName::matDir;
  const std::string fullMaterialBasePath = computeLocalPath(matDirPath);

  dxvk::env::createDirectory(matDirPath);
  for(const auto& [matId, matData] : exportData.materials) {
    // Build material stage
    const std::string matName = prefix::mat + matData.matName;
    const std::string matStageName = matName + ctx.extension;
    const std::string matStagePath = matDirPath + matStageName;
    pxr::UsdStageRefPtr matStage = findOpenOrCreateStage(matStagePath, true);
    assert(matStage);
    setCommonStageMetaData(matStage, exportData);

    // Add Looks + RootPrim prims
    const auto looksSdfPath = gStageRootPath.AppendChild(gTokLooks);
    const auto looksScopePrim = matStage->DefinePrim(looksSdfPath, gTokScope);
    assert(looksScopePrim);
    matStage->SetDefaultPrim(looksScopePrim);

    // Create material prim
    const auto matSdfPath = looksSdfPath.AppendElementString(matName);
    const auto matSchema = pxr::UsdShadeMaterial::Define(matStage, matSdfPath);
    assert(matSchema);
    const auto matPrim = matSchema.GetPrim();
    assert(matPrim);

    // Create shader prim under material prim
    static const pxr::TfToken kTokShader("Shader");
    const auto shaderPath = matPrim.GetPath().AppendChild(kTokShader);
    const auto shader = pxr::UsdShadeShader::Define(matStage, shaderPath);
    const auto shaderPrim = shader.GetPrim();
    assert(shaderPrim);

    // Create shader prim outputs attr
    static const pxr::TfToken kTokOutputsOutput("outputs:out");
    const auto outputsOutAttr =
      shaderPrim.CreateAttribute(kTokOutputsOutput, pxr::SdfValueTypeNames->Token, false, pxr::SdfVariabilityVarying);

    // Create and connect material outputs to shader outputs
    static const pxr::TfToken kTokOutputsMdlSurface("outputs:mdl:surface");
    const auto outputsMdlSurfaceAttr =
      matPrim.CreateAttribute(kTokOutputsMdlSurface, pxr::SdfValueTypeNames->Token, false, pxr::SdfVariabilityVarying);
    outputsMdlSurfaceAttr.AddConnection(outputsOutAttr.GetPath(), pxr::UsdListPositionFrontOfAppendList);

    // Set shader "Kind"
    static const pxr::TfToken kTokMaterial("Material");
    pxr::UsdModelAPI(shader).SetKind(kTokMaterial);

    // Create and set textures asset paths on material
    static const auto setTextureAttr =
      [](const pxr::UsdPrim& shaderPrim, const pxr::TfToken attrName, const std::string& relTexPath, const std::string& fullMaterialBasePath)
    {
      const auto attr = shaderPrim.CreateAttribute(pxr::TfToken(attrName), pxr::SdfValueTypeNames->Asset, false, pxr::SdfVariabilityVarying);
      assert(attr);
      const auto fullTexturePath = computeLocalPath(relTexPath);
      const auto relToMaterialsTexPath = std::filesystem::relative(fullTexturePath,fullMaterialBasePath).string();
      const bool bSetSuccessful = attr.Set(pxr::SdfAssetPath(relToMaterialsTexPath));
      assert(bSetSuccessful);
      static const pxr::TfToken kTokColorSpaceAuto("auto");
      attr.SetColorSpace(kTokColorSpaceAuto);
      return true;
    };
    static const pxr::TfToken kTokenInputsDiffuseTex("inputs:diffuse_texture");

    // Try to use an updated texture, if that doesn't work, try to use an old one
    setTextureAttr(shaderPrim, kTokenInputsDiffuseTex, matData.albedoTexPath, fullMaterialBasePath);

    // Create and set OmniPBR MDL boilerplate attributes on shader
    static const pxr::TfToken kTokInfoImplSource("info:implementationSource");
    static const pxr::TfToken kTokSourceAsset("sourceAsset");
    const auto infoImplSourceAttr =
      shaderPrim.CreateAttribute(kTokInfoImplSource, pxr::SdfValueTypeNames->Token, false, pxr::SdfVariabilityUniform);
    assert(infoImplSourceAttr);
    const bool bSetInfoImplSourceAttr = infoImplSourceAttr.Set(kTokSourceAsset);
    assert(bSetInfoImplSourceAttr);

    static const pxr::TfToken kTokInfoMdlSourceAsset("info:mdl:sourceAsset");

    static const pxr::SdfAssetPath kSdfAssetPathOmniPBR("./AperturePBR_Opacity.mdl");
    const auto infoMdlSourceAsset =
      shaderPrim.CreateAttribute(kTokInfoMdlSourceAsset, pxr::SdfValueTypeNames->Asset, false, pxr::SdfVariabilityUniform);
    assert(infoMdlSourceAsset);
    const bool bSetInfoMdlSourceAsset = infoMdlSourceAsset.Set(kSdfAssetPathOmniPBR);
    assert(bSetInfoMdlSourceAsset);

    static const pxr::TfToken kTokInfoMdlSourceAssetSubId("info:mdl:sourceAsset:subIdentifier");
    static const pxr::TfToken kTokOmniPBR("AperturePBR_Opacity");
    const auto infoImplSourceSubIdAttr =
      shaderPrim.CreateAttribute(kTokInfoMdlSourceAssetSubId, pxr::SdfValueTypeNames->Token, false, pxr::SdfVariabilityUniform);
    assert(infoImplSourceSubIdAttr);
    const bool bSetInfoMdlSourceAssetSubId = infoImplSourceSubIdAttr.Set(kTokOmniPBR);
    assert(bSetInfoMdlSourceAssetSubId);

    // Mark whether to enable varying opacity
    static const pxr::TfToken kTokEnableOpacity("enable_opacity");
    const auto enableOpacityAttr =
      shaderPrim.CreateAttribute(kTokEnableOpacity, pxr::SdfValueTypeNames->Bool, false, pxr::SdfVariabilityUniform);
    assert(enableOpacityAttr);
    const bool bSetEnableOpacityAttr = enableOpacityAttr.Set(matData.enableOpacity);
    assert(bSetEnableOpacityAttr);

    matStage->Save();
    
    // Cache material reference
    Reference matLssReference;
    matLssReference.stagePath = matStagePath;
    matLssReference.ogSdfPath = matSdfPath;

    // Build matSchema prim on instance stage
    if(ctx.instanceStage != nullptr) {
      const auto matInstanceSdfPath = gRootMaterialsPath.AppendElementString(matName);
      auto matInstanceSchema = pxr::UsdShadeMaterial::Define(ctx.instanceStage, matInstanceSdfPath);
      assert(matInstanceSchema);
      
      const std::string relMeshStagePath = commonDirName::matDir + matName + ctx.extension;
      auto matInstanceUsdReferences = matInstanceSchema.GetPrim().GetReferences();
      matInstanceUsdReferences.AddReference(relMeshStagePath, matSdfPath);
      
      matLssReference.instanceSdfPath = matInstanceSdfPath;
    }

    ctx.matReferences[matId] = matLssReference;
  }
  dxvk::Logger::debug("[GameExporter][" + exportData.debugId + "][exportMaterials] End");
}

void GameExporter::exportSkeletons(const Export& exportData, ExportContext& ctx) {
  dxvk::Logger::debug("[GameExporter][" + exportData.debugId + "][exportSkeletons] Begin");
  static const pxr::GfMatrix4d identity(1);
  const std::string relDirPath = commonDirName::skeletonDir + "/";
  const std::string dirPath = exportData.baseExportPath + "/" + relDirPath;
  const std::string fullStagePath = computeLocalPath(dirPath);
  dxvk::env::createDirectory(dirPath);
  for (const auto& [meshId, mesh] : exportData.meshes) {
    if (mesh.numBones == 0) {
      continue;
    }

    // Build skeleton stage
    const std::string name = prefix::skeleton + mesh.meshName;
    const std::string stagePath = dirPath + name + ctx.extension;
    pxr::UsdStageRefPtr stage = findOpenOrCreateStage(stagePath, true);
    assert(stage);
    setCommonStageMetaData(stage, exportData);

    pxr::VtDictionary customLayerData = stage->GetRootLayer()->GetCustomLayerData();
    for (auto& component : mesh.componentHashes) {
      customLayerData.SetValueAtPath(component.first, pxr::VtValue(component.second));
    }
    stage->GetRootLayer()->SetCustomLayerData(customLayerData);

    // Build skel root prim on stage
    const auto defaultPrimPath = gStageRootPath.AppendElementString(name);
    pxr::UsdSkelRoot skelRootSchema = pxr::UsdSkelRoot::Define(stage, defaultPrimPath);

    assert(skelRootSchema);
    stage->SetDefaultPrim(skelRootSchema.GetPrim());

    // Build skeleton prim under above xform
    const auto skeletonSdfPath = defaultPrimPath.AppendChild(gTokSkel);
    auto skelSchema = pxr::UsdSkelSkeleton::Define(stage, skeletonSdfPath);
    assert(skelSchema);


    // Set bindTransforms attribute
    auto bindTransformsAttr = skelSchema.CreateBindTransformsAttr();
    assert(bindTransformsAttr);
    ctx.skeletons[meshId] = generateSkeleton(mesh.numBones,
                                             mesh.bonesPerVertex,
                                             mesh.buffers.positionBufs.begin()->second,
                                             mesh.buffers.blendWeightBufs.empty() ? nullptr : &mesh.buffers.blendWeightBufs.begin()->second,
                                             mesh.buffers.blendIndicesBufs.empty() ? nullptr : &mesh.buffers.blendIndicesBufs.begin()->second);
    const Skeleton& skel = ctx.skeletons[meshId];
    // pxr::VtMatrix4dArray identities(mesh.numBones, pxr::GfMatrix4d(1));
    bindTransformsAttr.Set(skel.bindPose);

    // Set restTransforms attribute
    auto restTransformsAttr = skelSchema.CreateRestTransformsAttr();
    assert(restTransformsAttr);
    restTransformsAttr.Set(skel.restPose);

    // Set joints attribute on both the skeleton and the pose
    auto jointsAttr = skelSchema.CreateJointsAttr();
    assert(jointsAttr);
    jointsAttr.Set(skel.jointNames);

    stage->Save();

    // Build meshSchema prim on instance stage
    if (ctx.instanceStage != nullptr) {
      const std::string mesh_name = prefix::mesh + mesh.meshName;
      const std::string relSkelStagePath = relDirPath + name + ctx.extension;
      const pxr::SdfPath skelInstancePath = gRootMeshesPath.AppendElementString(mesh_name).AppendElementString(gTokSkel);

      auto skelSchema = pxr::UsdSkelSkeleton::Define(ctx.instanceStage, skelInstancePath);
      auto skelInstanceUsdReferences = skelSchema.GetPrim().GetReferences();
      skelInstanceUsdReferences.AddReference(relSkelStagePath, skeletonSdfPath);
    }
  }
  dxvk::Logger::debug("[GameExporter][" + exportData.debugId + "][exportSkeletons] End");
}

void GameExporter::exportMeshes(const Export& exportData, ExportContext& ctx) {
  dxvk::Logger::debug("[GameExporter][" + exportData.debugId + "][exportMeshes] Begin");
  static const pxr::GfMatrix4d identity(1);
  const std::string relMeshDirPath = commonDirName::meshDir + "/";
  const std::string meshDirPath = exportData.baseExportPath + "/" + relMeshDirPath;
  const std::string fullMeshStagePath = computeLocalPath(meshDirPath);
  dxvk::env::createDirectory(meshDirPath);
  for(const auto& [meshId,mesh] : exportData.meshes) {
    assert(mesh.numVertices > 0);
    assert(mesh.numIndices > 0);

    const bool isSkeleton = mesh.numBones > 0;

    // Build mesh stage
    const std::string meshName = prefix::mesh + mesh.meshName;
    const std::string meshStagePath = meshDirPath + meshName + ctx.extension;
    pxr::UsdStageRefPtr meshStage = findOpenOrCreateStage(meshStagePath, true);
    assert(meshStage);
    setCommonStageMetaData(meshStage, exportData);

    pxr::VtDictionary customLayerData = meshStage->GetRootLayer()->GetCustomLayerData();
    for (auto& component : mesh.componentHashes) {
      customLayerData.SetValueAtPath(component.first, pxr::VtValue(component.second));
    }
    meshStage->GetRootLayer()->SetCustomLayerData(customLayerData);

    // Build mesh xform prim on mesh stage, make it visible
    const auto meshXformSdfPath = gStageRootPath.AppendElementString(meshName);
    pxr::UsdGeomXformable meshXformSchema;
    if (isSkeleton) {
      meshXformSchema = pxr::UsdSkelRoot::Define(meshStage, meshXformSdfPath);
    } else {
      meshXformSchema = pxr::UsdGeomXform::Define(meshStage, meshXformSdfPath);
    }
    assert(meshXformSchema);
    meshStage->SetDefaultPrim(meshXformSchema.GetPrim());
    auto meshXformVisibilityAttr = meshXformSchema.CreateVisibilityAttr();
    assert(meshXformVisibilityAttr);
    meshXformVisibilityAttr.Set(gVisibilityInherited);

    // Build mesh geometry prim under above xform
    const auto meshSchemaSdfPath = meshXformSdfPath.AppendChild(gTokMesh);
    pxr::UsdGeomMesh meshSchema = pxr::UsdGeomMesh::Define(meshStage, meshSchemaSdfPath);
    pxr::UsdGeomPrimvarsAPI primvarsAPI(meshSchema.GetPrim());

    assert(meshSchema);
    auto meshVisibilityAttr = meshSchema.CreateVisibilityAttr();
    assert(meshVisibilityAttr);
    meshVisibilityAttr.Set(gVisibilityInherited);
    
    // Set double-sidedness attribute
    auto doubleSidedAttr = meshSchema.CreateDoubleSidedAttr();
    assert(doubleSidedAttr);
    doubleSidedAttr.Set(mesh.isDoubleSided);

    // Set orientation attribute
    auto orientationAttr = meshSchema.CreateOrientationAttr();
    assert(orientationAttr);
    orientationAttr.Set(pxr::VtValue(pxr::UsdGeomTokens->leftHanded));

    // Create corresponding attribute arrays using above populated VtArrays
    pxr::VtArray<int> faceVertexCounts;
    faceVertexCounts.assign(mesh.numIndices / 3, 3);
    auto faceVertexCountsAttr = meshSchema.CreateFaceVertexCountsAttr();
    assert(faceVertexCountsAttr);
    faceVertexCountsAttr.Set(faceVertexCounts);

    for (auto& pair : mesh.categoryFlags) {
      const auto attribute = meshSchema.GetPrim().CreateAttribute(pxr::TfToken(pair.first), pxr::SdfValueTypeNames->Bool, true, pxr::SdfVariabilityUniform);
      attribute.Set(pxr::VtValue(pair.second));
    }

    // Indices
    const bool reduce = exportData.meta.bReduceMeshBuffers;
    ReducedIdxBufSet reducedIdxBufSet = reduce ? reduceIdxBufferSet(mesh.buffers.idxBufs) : ReducedIdxBufSet();
    const std::map<float,IndexBuffer>& idxBufSet =
      reduce ? reducedIdxBufSet.bufSet : mesh.buffers.idxBufs;
    auto indexAttr = meshSchema.CreateFaceVertexIndicesAttr();
    assert(indexAttr);
    exportBufferSet(idxBufSet, indexAttr);
    // Vertices
    auto pointsAttr = meshSchema.CreatePointsAttr();
    assert(pointsAttr);
    exportBufferSet(reduce ? reduceBufferSet(mesh.buffers.positionBufs, reducedIdxBufSet) : mesh.buffers.positionBufs, pointsAttr);
    // Normals
    auto normalsAttr = meshSchema.CreateNormalsAttr();
    assert(normalsAttr);
    exportBufferSet(reduce ? reduceBufferSet(mesh.buffers.normalBufs, reducedIdxBufSet) : mesh.buffers.normalBufs, normalsAttr);
    // Set subdivision scheme to None (USD defaults to catmull clark)
    auto subdivAttr = meshSchema.CreateSubdivisionSchemeAttr();
    assert(subdivAttr);
    subdivAttr.Set(pxr::UsdGeomTokens->none);
    // Texture Coordinates
    static const pxr::TfToken kTokSt("st");
    auto stAttr = primvarsAPI.CreatePrimvar(kTokSt, pxr::SdfValueTypeNames->TexCoord2fArray, pxr::UsdGeomTokens->vertex);
    assert(stAttr);
    exportBufferSet(reduce ? reduceBufferSet(mesh.buffers.texcoordBufs, reducedIdxBufSet) : mesh.buffers.texcoordBufs, stAttr);

    // Vertex Colors
    if (mesh.buffers.colorBufs.size() > 0) {
      auto displayColorPrimvar = meshSchema.CreateDisplayColorPrimvar(pxr::UsdGeomTokens->vertex);
      auto displayOpacityPrimvar = meshSchema.CreateDisplayOpacityPrimvar(pxr::UsdGeomTokens->vertex);
      assert(displayColorPrimvar);
      assert(displayOpacityPrimvar);
      if (mesh.buffers.colorBufs.cbegin()->second.size() == 1) {
        // Constant Color
        displayColorPrimvar.SetInterpolation(pxr::UsdGeomTokens->constant);
        displayOpacityPrimvar.SetInterpolation(pxr::UsdGeomTokens->constant);
      }
      exportColorOpacityBufferSet(reduce ? reduceBufferSet(mesh.buffers.colorBufs, reducedIdxBufSet) : mesh.buffers.colorBufs, displayColorPrimvar, displayOpacityPrimvar);
    }
    
    if (isSkeleton) {
      pxr::UsdSkelBindingAPI skelBind = pxr::UsdSkelBindingAPI::Apply(meshSchema.GetPrim());

      auto jointWeightsAttr = skelBind.CreateJointWeightsPrimvar(0, mesh.bonesPerVertex);
      assert(jointWeightsAttr);
      exportBufferSet(reduce ? reduceBufferSet(mesh.buffers.blendWeightBufs, reducedIdxBufSet, mesh.bonesPerVertex) : mesh.buffers.blendWeightBufs, jointWeightsAttr);

      auto jointIndicesAttr = skelBind.CreateJointIndicesPrimvar(0, mesh.bonesPerVertex);
      assert(jointIndicesAttr);
      if (mesh.buffers.blendIndicesBufs.size() > 0) {
        exportBufferSet(reduce ? reduceBufferSet(mesh.buffers.blendIndicesBufs, reducedIdxBufSet, mesh.bonesPerVertex) : mesh.buffers.blendIndicesBufs, jointIndicesAttr);
      } else {
        // D3D9 allows for default bone indices of "0, 1, ... bonesPerVertex" if no joint indices are set.
        pxr::VtArray<int> defaultIndices(mesh.bonesPerVertex * mesh.numVertices);
        for (int i = 0; i < mesh.numVertices; ++i) {
          for (int j = 0; j < mesh.bonesPerVertex; ++j) {
            defaultIndices[i * mesh.bonesPerVertex + j] = j;
          }
        }
        jointIndicesAttr.Set(defaultIndices);
      }

      auto skelRel = skelBind.CreateSkeletonRel();
      skelRel.AddTarget(meshXformSdfPath.AppendChild(gTokSkel));
    }

    const bool bHasMat = mesh.matId != kInvalidId;
    const Reference& matLssReference = (bHasMat) ? ctx.matReferences[mesh.matId] : Reference();
    if(bHasMat) {
      const auto shaderMatSchema = pxr::UsdShadeMaterial::Define(meshStage, matLssReference.ogSdfPath);
      assert(shaderMatSchema);
      auto shaderMatUsdReferences = shaderMatSchema.GetPrim().GetReferences();
      const std::string fullMatStagePath = computeLocalPath(matLssReference.stagePath);
      const std::string relMatRefStagePath = std::filesystem::relative(fullMatStagePath,fullMeshStagePath).string();
      shaderMatUsdReferences.AddReference(relMatRefStagePath, matLssReference.ogSdfPath);
      pxr::UsdShadeMaterialBindingAPI(meshXformSchema.GetPrim()).Bind(shaderMatSchema);
    }

    // Kit metadata
    if(exportData.meta.bUseLssUsdPlugins) {
      meshXformSchema.GetPrim().SetMetadata(PXR_NS::SdfFieldKeys->Kind, PXR_NS::KindTokens->assembly);
      static const pxr::TfToken kTokHideInStageWindow("hide_in_stage_window");
      meshSchema.GetPrim().SetMetadata(kTokHideInStageWindow, true);
      static const pxr::TfToken kTokNoDelete("no_delete");
      meshSchema.GetPrim().SetMetadata(kTokNoDelete, true);
    }

    meshStage->Save();
    
    // Cache material reference
    Reference meshLssReference;
    meshLssReference.stagePath = meshStagePath;
    meshLssReference.ogSdfPath = meshXformSdfPath;
    
    // Build meshSchema prim on instance stage
    if(ctx.instanceStage != nullptr) {
      const auto meshInstanceXformSdfPath = gRootMeshesPath.AppendElementString(meshName);
      pxr::UsdGeomXformable meshInstanceXformSchema;
      if (isSkeleton) {
        meshInstanceXformSchema = pxr::UsdSkelRoot::Define(ctx.instanceStage, meshInstanceXformSdfPath);
      } else {
        meshInstanceXformSchema = pxr::UsdGeomXform::Define(ctx.instanceStage, meshInstanceXformSdfPath);
      }
      assert(meshInstanceXformSchema);
      
      const std::string relMeshStagePath = relMeshDirPath + meshName + ctx.extension;
      auto meshInstanceUsdReferences = meshInstanceXformSchema.GetPrim().GetReferences();
      meshInstanceUsdReferences.AddReference(relMeshStagePath);

      auto meshInstanceXformVisibilityAttr = meshInstanceXformSchema.CreateVisibilityAttr();
      assert(meshInstanceXformVisibilityAttr);
      meshInstanceXformVisibilityAttr.Set(gVisibilityInvisible);
      
      if(bHasMat) {
        const auto shaderMatInstanceSchema = pxr::UsdShadeMaterial::Get(ctx.instanceStage, matLssReference.instanceSdfPath);
        assert(shaderMatInstanceSchema);
        pxr::UsdShadeMaterialBindingAPI(meshInstanceXformSchema.GetPrim()).Bind(shaderMatInstanceSchema);
      }

      meshLssReference.instanceSdfPath = meshInstanceXformSdfPath;
    }
    
    ctx.meshReferences[meshId] = meshLssReference;
  }
  dxvk::Logger::debug("[GameExporter][" + exportData.debugId + "][exportMeshes] End");
}

GameExporter::ReducedIdxBufSet GameExporter::reduceIdxBufferSet(const std::map<float,IndexBuffer>& idxBufSet) {
  ReducedIdxBufSet reducedIdxBufSet;
  for(const auto& [timeCode, idxBuf] : idxBufSet) {
    const std::set<int> orderedIndices(idxBuf.cbegin(), idxBuf.cend());
    int newIdx = 0;
    ReducedIdxBufSet::IdxMap ogToRed;
    for(const auto index : orderedIndices) {
      ogToRed[index] = newIdx++;
    }
    for(const auto ogIdx : idxBuf) {
      const auto redIdx = ogToRed[ogIdx];
      assert(redIdx <= ogIdx);
      reducedIdxBufSet.bufSet[timeCode].push_back(redIdx);
      reducedIdxBufSet.redToOgSet[timeCode][redIdx] = ogIdx;
    }
  }
  return reducedIdxBufSet;
}

template<typename T>
std::map<float,pxr::VtArray<T>> GameExporter::reduceBufferSet(const std::map<float,pxr::VtArray<T>>& bufSet,
                                                              const ReducedIdxBufSet& reducedIdxBufSet,
                                                              size_t elemsPerIdx) {
  std::map<float,pxr::VtArray<T>> reducedBufSet;
  for(const auto& [timeCode, buf] : bufSet) {
    // There may not be a 1:1 mapping in timecodes b/w index buffers and other buffers
    float idxBufTimeCode = -1.f;
    if(reducedIdxBufSet.bufSet.size() > 1) {
      const auto iPair_timeCode_idxBuf = reducedIdxBufSet.bufSet.lower_bound(timeCode);
      assert(iPair_timeCode_idxBuf != reducedIdxBufSet.bufSet.cend());
      const float idxTimeCode = iPair_timeCode_idxBuf->first;
      idxBufTimeCode = iPair_timeCode_idxBuf->first;
    } else {
      idxBufTimeCode = reducedIdxBufSet.bufSet.cbegin()->first;
    }
    assert(idxBufTimeCode >= 0.f);

    // Create a scratch space to assign values for new, reduce VtArray, in case there are holes in the indices
    const auto& redIdxToOgIdx = reducedIdxBufSet.redToOgSet.at(idxBufTimeCode);
    const int numIdxs = redIdxToOgIdx.size();
    const size_t numElems = numIdxs * elemsPerIdx;
    T* const reducedBufScratch = new T[numElems];
    
    // Init potential holes to 0
    memset(reducedBufScratch, 0, sizeof(T) * numElems);
    for(const auto [redIndex,ogIndex] : redIdxToOgIdx) {
      for (int elemNum = 0; elemNum < elemsPerIdx; ++elemNum) {
        const auto ogIdx = (ogIndex * elemsPerIdx) + elemNum;
        const auto redIdx = (redIndex * elemsPerIdx) + elemNum;
        assert(redIdx <= ogIdx);
        reducedBufScratch[redIdx] = buf[ogIdx];
      }
    }
    reducedBufSet[timeCode] = pxr::VtArray<T>(reducedBufScratch, reducedBufScratch + numElems);
  }
  return reducedBufSet;
}

template<typename T>
void GameExporter::exportBufferSet(const std::map<float,pxr::VtArray<T>>& bufSet,
                                       pxr::UsdAttribute attr) {
  if(bufSet.size() == 1) {
    attr.Set(bufSet.cbegin()->second);
  } else {
    for(const auto& [timeCode, buf] : bufSet) {
      const auto usdTimeCode = pxr::UsdTimeCode(timeCode);
      attr.Set(buf, usdTimeCode);
    }
  }
}

template<typename T>
void GameExporter::exportColorOpacityBufferSet(const std::map<float, pxr::VtArray<T>>& bufSet, pxr::UsdAttribute color, pxr::UsdAttribute opacity) {
  pxr::VtArray<pxr::GfVec3f> colorArray;
  pxr::VtArray<float> opacityArray;

  if (bufSet.size() == 1) {
    for (const T& element : bufSet.cbegin()->second) {
      colorArray.emplace_back(pxr::GfVec3f(element[0], element[1], element[2]));
      opacityArray.emplace_back(element[3]);
    }
    color.Set(colorArray);
    opacity.Set(opacityArray);
  } else {
    for (const auto& [timeCode, buf] : bufSet) {
      for (const T& element : buf) {
        colorArray.emplace_back(pxr::GfVec3f(element[0], element[1], element[2]));
        opacityArray.emplace_back(element[3]);
      }
      const auto usdTimeCode = pxr::UsdTimeCode(timeCode);
      color.Set(colorArray, usdTimeCode);
      opacity.Set(opacityArray, usdTimeCode);
    }
  }
}

void GameExporter::exportInstances(const Export& exportData, ExportContext& ctx) {
  assert(exportData.bExportInstanceStage);
  dxvk::Logger::debug("[GameExporter][" + exportData.debugId + "][exportInstances] Begin");
  for(const auto& [instId,instanceData] : exportData.instances) {
    // Build base Xform prim for instance to reside in
    auto instanceName = (instanceData.isSky ? "sky_" : "inst_") + std::string(instanceData.instanceName);
    pxr::SdfPath fullInstancePath = gRootInstancesPath.AppendElementString(instanceName);

    const bool isSkeleton = !instanceData.boneXForms.empty();

    pxr::UsdGeomXformable instanceXform;
    if (isSkeleton) {
      instanceXform = pxr::UsdSkelRoot::Define(ctx.instanceStage, fullInstancePath);
    } else {
      instanceXform = pxr::UsdGeomXform::Define(ctx.instanceStage, fullInstancePath);
    }
    assert(instanceXform);

    // Attach reference to mesh in question
    const Reference& meshLssReference = ctx.meshReferences[instanceData.meshId];
    auto instanceUsdReferences = instanceXform.GetPrim().GetReferences();
    instanceUsdReferences.AddInternalReference(meshLssReference.instanceSdfPath);
    
    // Set instanced mesh to now be visible
    auto visibilityAttr = instanceXform.CreateVisibilityAttr();
    assert(visibilityAttr);
    visibilityAttr.Set(gVisibilityInherited);
    
    // Hide the original sky mesh(s) since it may block the sky dome light and other lights
    // and cast unwanted shadows.
    if (instanceData.isSky) {
      visibilityAttr.Set(gVisibilityInvisible);
    }

    if(instanceData.matId != kInvalidId) {
      // Bind material associated with above mesh
      const Reference& matLssReference = ctx.matReferences[instanceData.matId];
      const auto shaderMatSchema = pxr::UsdShadeMaterial::Get(ctx.instanceStage, matLssReference.instanceSdfPath);
      assert(shaderMatSchema);
      pxr::UsdShadeMaterialBindingAPI(instanceXform.GetPrim()).Bind(shaderMatSchema);
    }

    if (isSkeleton) {
      // Set instance skeleton pose / animation
      const auto skelPoseSdfPath = fullInstancePath.AppendChild(gTokPose);
      const auto skelSkelSdfPath = fullInstancePath.AppendChild(gTokSkel);
      auto skelAnimationSchema = pxr::UsdSkelAnimation::Define(ctx.instanceStage, skelPoseSdfPath);
      assert(skelAnimationSchema);
      const lss::Skeleton& skel = ctx.skeletons[instanceData.meshId];

      skelAnimationSchema.CreateJointsAttr().Set(skel.jointNames);

      // set the rotations, scales, and translations attributes on the pose
      for (auto sample : instanceData.boneXForms) {
        skelAnimationSchema.SetTransforms(
            sanitizeBoneXforms(sample.xforms, skel.bindPose, exportData.meta),
            exportData.meta.numFramesCaptured == 1 ? pxr::UsdTimeCode::Default() : pxr::UsdTimeCode(sample.time));
      }

      pxr::UsdPrim skelPrim = ctx.instanceStage->GetPrimAtPath(skelSkelSdfPath);
      pxr::UsdSkelBindingAPI skelBindingSchema = pxr::UsdSkelBindingAPI::Apply(skelPrim);
      auto animationSource = skelBindingSchema.CreateAnimationSourceRel();
      animationSource.SetTargets({skelPoseSdfPath});
    } else {
      const auto meshSchemaSdfPath = fullInstancePath.AppendChild(gTokMesh);
      pxr::UsdGeomMesh meshSchema = pxr::UsdGeomMesh::Define(ctx.instanceStage, meshSchemaSdfPath);
      pxr::UsdGeomPrimvarsAPI primvarsAPI(meshSchema.GetPrim());

#define _SetDrawMetadata(name, type)  primvarsAPI.CreatePrimvar(pxr::TfToken("_remix_metadata:" #name), pxr::SdfValueTypeNames->##type).Set(pxr::VtValue(instanceData.metadata.##name))
      _SetDrawMetadata(alphaTestEnabled, Bool);
      _SetDrawMetadata(alphaTestReferenceValue, UInt);
      _SetDrawMetadata(alphaTestCompareOp, UInt);
      _SetDrawMetadata(alphaBlendEnabled, Bool);
      _SetDrawMetadata(srcColorBlendFactor, UInt);
      _SetDrawMetadata(dstColorBlendFactor, UInt);
      _SetDrawMetadata(colorBlendOp, UInt);
      _SetDrawMetadata(textureColorArg1Source, UInt);
      _SetDrawMetadata(textureColorArg2Source, UInt);
      _SetDrawMetadata(textureColorOperation, UInt);
      _SetDrawMetadata(textureAlphaArg1Source, UInt);
      _SetDrawMetadata(textureAlphaArg2Source, UInt);
      _SetDrawMetadata(textureAlphaOperation, UInt);
      _SetDrawMetadata(tFactor, UInt);
      _SetDrawMetadata(isTextureFactorBlend, Bool);
#undef _SetDrawMetadata
    }

    setTimeSampledXforms<true>(ctx.instanceStage, fullInstancePath, instanceData.firstTime, instanceData.finalTime, instanceData.xforms, exportData.meta);
    setVisibilityTimeSpan(ctx.instanceStage, fullInstancePath, instanceData.firstTime, instanceData.finalTime, exportData.meta.numFramesCaptured);
  }
  dxvk::Logger::debug("[GameExporter][" + exportData.debugId + "][exportInstances] End");
}

void GameExporter::exportCamera(const Export& exportData, ExportContext& ctx) {
  dxvk::Logger::debug("[GameExporter][" + exportData.debugId + "][exportCamera] Begin");
  static const pxr::TfToken kTokCamera("Camera");
  const pxr::SdfPath cameraSdfPath = gRootNodePath.AppendChild(kTokCamera);
  auto geomCamera = pxr::UsdGeomCamera::Define(ctx.instanceStage, cameraSdfPath);

  // Create Gf Camera which will convert FOV + Aspect Ratio -> Usd Camera Attributes
  pxr::GfCamera simpleCam;
  simpleCam.SetPerspectiveFromAspectRatioAndFieldOfView(
    exportData.camera.aspectRatio,
    // Note: USD expects vertical FoV in this case in degrees, not radians like we store.
    exportData.camera.fov * (180.0f / M_PI),
    pxr::GfCamera::FOVVertical
  );

  // Set horizontal aperture
  auto horizontalAperture = geomCamera.CreateHorizontalApertureAttr();
  horizontalAperture.Set(simpleCam.GetHorizontalAperture());

  // Set Vertical aperture
  auto verticalAperture = geomCamera.CreateVerticalApertureAttr();
  float verticalApertureVal = simpleCam.GetVerticalAperture();
  if(exportData.camera.bFlipVertAperture) {
    verticalApertureVal *= (-1.f);
  }
  verticalAperture.Set(verticalApertureVal);

  // Set focal length
  auto focalLength = geomCamera.CreateFocalLengthAttr();
  focalLength.Set(simpleCam.GetFocalLength());

  // Set clipping range
  auto clippingPlane = geomCamera.CreateClippingRangeAttr();
  clippingPlane.Set(pxr::GfVec2f(exportData.camera.nearPlane, exportData.camera.farPlane));
  
  setTimeSampledXforms<false>(ctx.instanceStage, cameraSdfPath, exportData.camera.firstTime, exportData.camera.finalTime, exportData.camera.xforms, exportData.meta);

  // Must modify here, since there may be existing data set earlier
  pxr::VtDictionary customLayerData = ctx.instanceStage->GetRootLayer()->GetCustomLayerData();
  pxr::VtDictionary cameraSetticsDict;
  cameraSetticsDict.SetValueAtPath("boundCamera",pxr::VtValue(cameraSdfPath.GetString()));
  customLayerData.SetValueAtPath("cameraSettings",pxr::VtValue(cameraSetticsDict));
  ctx.instanceStage->GetRootLayer()->SetCustomLayerData(customLayerData);
  dxvk::Logger::debug("[GameExporter][" + exportData.debugId + "][exportCamera] End");
}

void GameExporter::exportSphereLights(const Export& exportData, ExportContext& ctx) {
  const std::string relLightDirPath = commonDirName::lightDir + "/";
  const std::string lightDirPath = exportData.baseExportPath + "/" + relLightDirPath;
  const std::string fullLightStagePath = computeLocalPath(lightDirPath);
  dxvk::Logger::debug("[GameExporter][" + exportData.debugId + "][exportSphereLights] Begin");
  for(const auto& [id,sphereLightData] : exportData.sphereLights) {
    // Build light stage
    const std::string lightName = prefix::light + sphereLightData.lightName;
    const std::string lightStagePath = lightDirPath + lightName + ctx.extension;
    pxr::UsdStageRefPtr lightStage = findOpenOrCreateStage(lightStagePath, true);
    assert(lightStage);
    setCommonStageMetaData(lightStage, exportData);

    // Build sphere light prim
    const auto lightAssetSdfPath = gStageRootPath.AppendElementString(lightName);
    auto sphereLight = pxr::UsdLuxSphereLight::Define(lightStage, lightAssetSdfPath);
    assert(sphereLight);
    lightStage->SetDefaultPrim(sphereLight.GetPrim());
    auto colorAttr = sphereLight.CreateColorAttr();
    assert(colorAttr);
    colorAttr.Set(pxr::GfVec3f(sphereLightData.color[0], sphereLightData.color[1], sphereLightData.color[2]));

    auto intensityAttr = sphereLight.CreateIntensityAttr();
    assert(intensityAttr);
    intensityAttr.Set(sphereLightData.intensity);

    auto radiusAttr = sphereLight.CreateRadiusAttr();
    assert(radiusAttr);
    radiusAttr.Set(sphereLightData.radius);

    if (sphereLightData.shapingEnabled) {
      auto shaping = pxr::UsdLuxShapingAPI(sphereLight.GetPrim());

      auto coneAngleAttr = shaping.CreateShapingConeAngleAttr();
      assert(coneAngleAttr);
      coneAngleAttr.Set(sphereLightData.coneAngle);
      
      auto coneSoftnessAttr = shaping.CreateShapingConeSoftnessAttr();
      assert(coneSoftnessAttr);
      coneSoftnessAttr.Set(sphereLightData.coneSoftness);
      
      auto FocusExponentAttr = shaping.CreateShapingFocusAttr();
      assert(FocusExponentAttr);
      FocusExponentAttr.Set(sphereLightData.focusExponent);

      shaping.Apply(sphereLight.GetPrim());
    }

    setTimeSampledXforms<false>(lightStage, lightAssetSdfPath, sphereLightData.firstTime, sphereLightData.finalTime, sphereLightData.xforms, exportData.meta);
    
    pxr::UsdLuxLightAPI lightAPI(sphereLight.GetPrim());
    setLightIntensityOnTimeSpan(lightAPI, sphereLightData.intensity, sphereLightData.firstTime, sphereLightData.finalTime, exportData.meta.numFramesCaptured);
    lightAPI.Apply(sphereLight.GetPrim());

    lightStage->Save(); 

    // Cache light reference
    Reference lightLssReference;
    lightLssReference.stagePath = lightStagePath;
    lightLssReference.ogSdfPath = lightAssetSdfPath;

    // Build sphere light prim on instance stage
    if(ctx.instanceStage != nullptr) {
      const pxr::SdfPath fullSphereLightPath = gRootLightsPath.AppendElementString(lightName);
      auto sphereLightInstance = pxr::UsdLuxSphereLight::Define(ctx.instanceStage, fullSphereLightPath);

      const std::string relLightStagePath = relLightDirPath + lightName + ctx.extension;
      auto lightInstanceUsdReferences = sphereLightInstance.GetPrim().GetReferences();
      lightInstanceUsdReferences.AddReference(relLightStagePath);
    }
  }
  dxvk::Logger::debug("[GameExporter][" + exportData.debugId + "][exportSphereLights] End");
}

void GameExporter::exportDistantLights(const Export& exportData, ExportContext& ctx) {
  dxvk::Logger::debug("[GameExporter][" + exportData.debugId + "][exportDistantLights] Begin");
  for(const auto& [id,distantLightData] : exportData.distantLights) {
    // Build distant light prim
    auto distantLightName = lss::prefix::light + std::string(distantLightData.lightName);
    const pxr::SdfPath distantLightPath = gRootLightsPath.AppendElementString(distantLightName);
    auto distantLightSchema = pxr::UsdLuxDistantLight::Define(ctx.instanceStage, distantLightPath);
    assert(distantLightSchema);

    auto colorAttr = distantLightSchema.CreateColorAttr();
    assert(colorAttr);
    colorAttr.Set(pxr::GfVec3f(distantLightData.color[0], distantLightData.color[1], distantLightData.color[2]));
    
    auto intensityAttr = distantLightSchema.CreateIntensityAttr();
    assert(intensityAttr);

    auto angleAttr = distantLightSchema.CreateAngleAttr();
    assert(angleAttr);
    angleAttr.Set(distantLightData.angle);

    static const pxr::GfVec3d distantLightDefault(0.0,0.0,-1.0);
    const auto directionQuatF = pxr::GfQuatf{pxr::GfRotation(distantLightDefault, distantLightData.direction).GetQuat()};
    auto orientAttr = distantLightSchema.AddOrientOp();
    assert(orientAttr);
    orientAttr.Set(directionQuatF);

    pxr::UsdLuxLightAPI lightAPI(distantLightSchema.GetPrim());
    setLightIntensityOnTimeSpan(lightAPI, distantLightData.intensity, distantLightData.firstTime, distantLightData.finalTime, exportData.meta.numFramesCaptured);
    lightAPI.Apply(distantLightSchema.GetPrim());
  }
  dxvk::Logger::debug("[GameExporter][" + exportData.debugId + "][exportDistantLights] End");
}

void GameExporter::exportSky(const Export& exportData, ExportContext& ctx) {
  if (exportData.bakedSkyProbePath.empty()) {
    return;
  }

  dxvk::Logger::debug("[GameExporter][" + exportData.debugId + "][exportSky] Begin");

  const pxr::SdfPath domeLightPath = gRootLightsPath.AppendElementString("SkyDome_Non_Replaceable");
  auto domeLightSchema = pxr::UsdLuxDomeLight::Define(ctx.instanceStage, domeLightPath);
  assert(domeLightSchema);

  auto textureAttr = domeLightSchema.CreateTextureFileAttr();
  assert(textureAttr);

  const auto fullBasePath = computeLocalPath(exportData.baseExportPath);
  const auto fullTexturePath = computeLocalPath(exportData.bakedSkyProbePath);
  const auto relToMaterialsTexPath = std::filesystem::relative(fullTexturePath, fullBasePath).string();
  const bool bSetSuccessful = textureAttr.Set(pxr::SdfAssetPath(relToMaterialsTexPath));
  assert(bSetSuccessful);

  auto formatAttr = domeLightSchema.CreateTextureFormatAttr();
  assert(formatAttr);
  formatAttr.Set(pxr::TfToken("latlong"));

  domeLightSchema.OrientToStageUpAxis();

  dxvk::Logger::debug("[GameExporter][" + exportData.debugId + "][exportSky] End");
}

// Extract Euler angles from a rotation matrix factored as RxRyRz
// https://www.geometrictools.com/Documentation/EulerAngles.pdf
// Returns a vector with XYZ angles in degrees.
template<typename V, typename M>
static V ToEuler(const M& m) {
  V euler;

  if (m[2][0] < 1) {
    if (m[2][0] > -1) {
      euler[1] = asin(m[2][0]);
      euler[0] = atan2(-m[2][1], m[2][2]);
      euler[2] = atan2(-m[1][0], m[0][0]);
    } else {
      euler[1] = V::ScalarType(-M_PI / 2);
      euler[0] = -atan2(m[0][1], m[1][1]);
      euler[2] = 0;
    }
  } else {
    euler[1] = V::ScalarType(M_PI / 2);
    euler[0] = atan2(m[0][1], m[1][1]);
    euler[2] = 0;
  }

  return euler * 180.0 / M_PI;
}

// Compares two matrices approximately.
template<typename M>
static bool CompareApprox(const M& a, const M& b, double tolerance) {
  constexpr size_t numElements = M::numRows * M::numColumns;

  size_t eq = 0;
  for (size_t n = 0; n < numElements && eq == n; n++) {
    eq += fabs(a.data()[n] - b.data()[n]) < tolerance ? 1 : 0;
  }

  return eq == numElements;
}

// Extracts Euler rotation angles from improper rotation matrix and a mirror plane.
// When DoCheck is true, reconstructs the transform back using USD method and checks it
// against the input transform. The comparision result is returned.
// When DoCheck is false no check will be performed and function result will be always true.
template<bool DoCheck>
static bool ExtractEulerImproper(pxr::GfVec3f& rotation,
                                 const pxr::GfMatrix3d& rImproper,
                                 const pxr::GfVec3d& mirrorPlane) {
  // Make mirror transform matrix
  pxr::GfMatrix3d p(mirrorPlane);

  // Remove mirroring from improper rotation matrix and make it
  // proper rotation matrix to extract Euler angles
  auto rProper = p * rImproper;

  // Extract Euler angles
  rotation = ToEuler<pxr::GfVec3f>(rProper);

  if (DoCheck) {
    // Reconstruct the rotation matrix back from Euler angles using USD method
    rProper = pxr::UsdGeomXformCommonAPI::GetRotationTransform(
      rotation, pxr::UsdGeomXformCommonAPI::RotationOrderZYX).ExtractRotationMatrix();

    // Reconstructed matrix comparision tolerance. Can be a config option.
    constexpr double tolerance = 0.000001;

    // Make the resonstructed matrix improper for checking against the input matrix
    const auto rNewImproper = rProper * p;

    return CompareApprox(rImproper, rNewImproper, tolerance);
  }

  return true;
}

template<bool IsInstance>
void GameExporter::setTimeSampledXforms(const pxr::UsdStageRefPtr stage,
                                        const pxr::SdfPath sdfPath,
                                        const float firstTime,
                                        const float finalTime,
                                        const SampledXforms& xforms,
                                        const Export::Meta& meta,
                                        const bool teleportAway) {
  assert(stage);
  assert(sdfPath != pxr::SdfPath());
  assert(xforms.size() > 0);
  auto geomXform = pxr::UsdGeomXformCommonAPI::Get(stage, sdfPath);
  assert(geomXform);

  const bool isSingleFrame = meta.numFramesCaptured == 1;

  // [TODO]: make this game-programmable via RTX settings
  static const pxr::GfVec3d defaultXform(-10000.0, -10000.0, -10000.0);
  // If the first time this object is seen is not at t=0, we need it to be not visible
  if(teleportAway && (firstTime > 0.0)) {
    geomXform.SetTranslate(defaultXform, isSingleFrame ? pxr::UsdTimeCode::Default() : pxr::UsdTimeCode(0.0));
  }
  for(const auto& sampledXform : xforms) { 
    const pxr::UsdTimeCode timeCode = isSingleFrame ? pxr::UsdTimeCode::Default() : pxr::UsdTimeCode(sampledXform.time);

    const auto xform = meta.isLHS ? ToRHS(sampledXform.xform) : sampledXform.xform;

    const pxr::GfVec3d translation = xform.ExtractTranslation();
    pxr::GfVec3f scale(xform.GetRow3(0).GetLength(), xform.GetRow3(1).GetLength(), xform.GetRow3(2).GetLength());

    // Since scale signs cannot be definitely found from xform decompose
    // we need to force negative Z scale for instances when converting to RHS
    if (meta.isLHS && IsInstance) {
      scale[2] = -scale[2];
    }

    // Euler angles
    pxr::GfVec3f rotation;

    // Extract pure rotation matrix from xform
    const auto r = xform.GetOrthonormalized().ExtractRotationMatrix();

    if (r.GetHandedness() > 0) {
      // Proper pure rotation - easy case.
      rotation = ToEuler<pxr::GfVec3f>(r);
    } else {
      // Attempt to handle improper rotations (https://en.wikipedia.org/wiki/Improper_rotation)

      dxvk::Logger::warn("[GameExporter] The xform at '" + sdfPath.GetString() +
                         "' is not orientation-preserving. Attempting to find a decomposition solution.");

      // Attempt to search for signs of mirror plane
      // Start by flipping by all three axes since it should be the actual solution in most cases
      int32_t signsPermutation = 0b0111;
      pxr::GfVec3d n(-1.0);

      bool found = false;
      while (signsPermutation >= 0) {
        if (ExtractEulerImproper<true>(rotation, r, n)) {
          found = true;
          break;
        }

        signsPermutation -= 1;
        n = pxr::GfVec3d((signsPermutation & 0b001) ? -1.0 : 1.0,
                         (signsPermutation & 0b010) ? -1.0 : 1.0,
                         (signsPermutation & 0b100) ? -1.0 : 1.0);
      }

      if (!found) {
        dxvk::Logger::warn("[GameExporter] The rotoinversion xform decomposition was not found for '" +
                            sdfPath.GetString() + "'. Instance transform is NOT correct.");
      }

      // Apply mirroring coefficients via scale
      scale[0] *= n[0];
      scale[1] *= n[1];
      scale[2] *= n[2];

      if (meta.isZUp && !IsInstance && scale[2] < 0) {
        // TODO(iterentiev): why is this neccessary for cameras in some games. Make a config?
        scale[2] = -scale[2];
      }
    }

    geomXform.SetTranslate(translation, timeCode);
    geomXform.SetRotate(rotation, pxr::UsdGeomXformCommonAPI::RotationOrderZYX, timeCode);
    geomXform.SetScale(scale, timeCode);
  }
  // If the entity stops existing midway through capture, move it to where it's invisible
  if(teleportAway) {
    geomXform.SetTranslate(defaultXform, isSingleFrame ? pxr::UsdTimeCode::Default() : pxr::UsdTimeCode(std::nextafter(finalTime, finalTime + 1.0)));
  }
}

void GameExporter::setVisibilityTimeSpan(const pxr::UsdStageRefPtr stage,
                                      const pxr::SdfPath sdfPath,
                                      const double firstTime,
                                      const double finalTime,
                                      const size_t numFramesCaptured) {
  const bool isSingleFrame = numFramesCaptured == 1;
  if (!isSingleFrame) {
    auto geomImageSchema = pxr::UsdGeomImageable::Get(stage, sdfPath);
    assert(geomImageSchema);
    auto visibilityAttr = geomImageSchema.GetVisibilityAttr();
    if(!visibilityAttr) {
      visibilityAttr = geomImageSchema.CreateVisibilityAttr();
    }
    assert(visibilityAttr);
    static const pxr::TfToken kTokVisInherited("inherited");
    static const pxr::TfToken kTokVisInvisible("invisible");
    if(firstTime != 0.0) {
      visibilityAttr.Set(kTokVisInvisible, pxr::UsdTimeCode(0.0));
    }
    visibilityAttr.Set(kTokVisInherited, pxr::UsdTimeCode(firstTime));
    visibilityAttr.Set(kTokVisInherited, pxr::UsdTimeCode(finalTime));
    visibilityAttr.Set(kTokVisInvisible, pxr::UsdTimeCode(std::nextafter(finalTime, finalTime + 1.0)));
  }
}

void GameExporter::setLightIntensityOnTimeSpan(const pxr::UsdLuxLightAPI& luxLight,
                                               const float defaultLightIntensity,
                                               const double firstTime,
                                               const double finalTime,
                                               const size_t numFramesCaptured) {
  const bool isSingleFrame = numFramesCaptured == 1;
  auto intensityAttr = luxLight.GetIntensityAttr();
  if(!intensityAttr) {
    intensityAttr = luxLight.CreateIntensityAttr();
  }
  assert(intensityAttr);
  if (isSingleFrame) {
    intensityAttr.Set(defaultLightIntensity);
  } else {
    if(firstTime != 0.0) {
      intensityAttr.Set(0.f, pxr::UsdTimeCode(0.0));
    }
    intensityAttr.Set(defaultLightIntensity, pxr::UsdTimeCode(firstTime));
    intensityAttr.Set(defaultLightIntensity, pxr::UsdTimeCode(finalTime));
    intensityAttr.Set(0.f, pxr::UsdTimeCode(std::nextafter(finalTime, finalTime + 1.0)));
  }
}

pxr::UsdStageRefPtr GameExporter::findOpenOrCreateStage(const std::string path, const bool bClearIfExists) {
    const bool bLayerAlreadyExists = pxr::TfIsFile(path);
    pxr::SdfLayerRefPtr alreadyExistentLayer;
    if(bLayerAlreadyExists) {
      alreadyExistentLayer = pxr::SdfLayer::FindOrOpen(path);
      assert(alreadyExistentLayer);
      if(bClearIfExists) {
        alreadyExistentLayer->Clear();
      }
    }
    auto stage = (bLayerAlreadyExists) ? pxr::UsdStage::Open(alreadyExistentLayer) : pxr::UsdStage::CreateNew(path);
    assert(stage);
    return stage;
}

}
