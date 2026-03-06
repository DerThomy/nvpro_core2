/*
 * Copyright (c) 2023-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2023-2025, NVIDIA CORPORATION.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "slang.hpp"

#ifdef __ANDROID__
#include <android/asset_manager.h>
#include <cstring>

// Define the static member variable
AAssetManager* nvslang::SlangCompiler::g_assetManager = nullptr;

static bool sl_uuid_match(const SlangUUID& a, const SlangUUID& b) {
    return std::memcmp(&a, &b, sizeof(SlangUUID)) == 0;
}

// 1. A custom Blob to hold the loaded asset data in memory
class AndroidAssetBlob : public ISlangBlob {
private:
    long m_refCount = 1;
    void* m_data;
    size_t m_size;

public:
    AndroidAssetBlob(void* data, size_t size) : m_data(data), m_size(size) {}
    virtual ~AndroidAssetBlob() { delete[] static_cast<char*>(m_data); }

    // COM Boilerplate using the correct macros from slang.h
    SLANG_NO_THROW SlangResult SLANG_MCALL queryInterface(SlangUUID const& uuid, void** outObject) override {
        if (sl_uuid_match(uuid, SLANG_UUID_ISlangUnknown) || 
            sl_uuid_match(uuid, SLANG_UUID_ISlangBlob)) {
            *outObject = static_cast<ISlangBlob*>(this);
            addRef();
            return SLANG_OK;
        }
        return SLANG_E_NO_INTERFACE;
    }
    SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return ++m_refCount; }
    SLANG_NO_THROW uint32_t SLANG_MCALL release() override {
        if (--m_refCount == 0) { delete this; return 0; }
        return m_refCount;
    }

    // ISlangBlob implementation
    SLANG_NO_THROW void const* SLANG_MCALL getBufferPointer() override { return m_data; }
    SLANG_NO_THROW size_t SLANG_MCALL getBufferSize() override { return m_size; }
};

// 2. The custom File System to intercept Slang's #include requests
class AndroidSlangFileSystem : public ISlangFileSystem {
private:
    long m_refCount = 1;
    AAssetManager* m_assetManager = nullptr;

public:
    AndroidSlangFileSystem(AAssetManager* mgr) : m_assetManager(mgr) {}

    // COM Boilerplate using the correct macros
    SLANG_NO_THROW SlangResult SLANG_MCALL queryInterface(SlangUUID const& uuid, void** outObject) override {
        if (sl_uuid_match(uuid, SLANG_UUID_ISlangUnknown) || 
            sl_uuid_match(uuid, ISlangCastable::getTypeGuid()) ||
            sl_uuid_match(uuid, SLANG_UUID_ISlangFileSystem)) {
            *outObject = static_cast<ISlangFileSystem*>(this);
            addRef();
            return SLANG_OK;
        }
        return SLANG_E_NO_INTERFACE;
    }
    SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return ++m_refCount; }
    SLANG_NO_THROW uint32_t SLANG_MCALL release() override {
        if (--m_refCount == 0) { delete this; return 0; }
        return m_refCount;
    }

    // ISlangCastable implementation (Required because ISlangFileSystem inherits from it)
    SLANG_NO_THROW void* SLANG_MCALL castAs(const SlangUUID& guid) override {
        if (sl_uuid_match(guid, SLANG_UUID_ISlangUnknown) || 
            sl_uuid_match(guid, ISlangCastable::getTypeGuid()) ||
            sl_uuid_match(guid, SLANG_UUID_ISlangFileSystem)) {
            return static_cast<ISlangFileSystem*>(this);
        }
        return nullptr;
    }

    // Intercepts file load requests (like #includes)
    SLANG_NO_THROW SlangResult SLANG_MCALL loadFile(const char* path, ISlangBlob** outBlob) override {
        if (!m_assetManager) return SLANG_E_NOT_FOUND;

        std::string assetPath = std::filesystem::path(path).lexically_normal().generic_string();
        if (!assetPath.empty() && assetPath[0] == '/') assetPath.erase(0, 1);

        AAsset* asset = AAssetManager_open(m_assetManager, assetPath.c_str(), AASSET_MODE_BUFFER);
        if (!asset) {
            return SLANG_E_NOT_FOUND;
        }

        size_t length = AAsset_getLength(asset);
        char* content = new char[length];
        AAsset_read(asset, content, length);
        AAsset_close(asset);

        // Pass ownership of the memory to our custom Blob
        *outBlob = new AndroidAssetBlob(content, length);
        return SLANG_OK;
    }
};
#endif

nvslang::SlangCompiler::SlangCompiler(bool enableGLSL)
{
  SlangGlobalSessionDesc desc = {};
  desc.structureSize = sizeof(SlangGlobalSessionDesc);
  desc.apiVersion = SLANG_API_VERSION;
  desc.enableGLSL = enableGLSL;

  // 2. Create the session and catch any failures
  SlangResult res = slang::createGlobalSession(&desc, m_globalSession.writeRef());
  
  if (SLANG_FAILED(res) || !m_globalSession) {
      const char* errMsg = slang::getLastInternalErrorMessage();
      LOGE("FATAL: Failed to create Slang Global Session!\nError Code: 0x%X\nSlang Error: %s", res, errMsg ? errMsg : "Unknown");
  }
}

void nvslang::SlangCompiler::defaultTarget()
{
  m_targets.push_back({
      .format                      = SLANG_SPIRV,
      .profile                     = m_globalSession->findProfile("spirv_1_5+vulkan_1_3"),
      .flags                       = 0,
      .forceGLSLScalarBufferLayout = true,
  });
}

void nvslang::SlangCompiler::defaultOptions()
{
  m_options.push_back({slang::CompilerOptionName::EmitSpirvViaGLSL, {slang::CompilerOptionValueKind::Int, 1}});
  m_options.push_back({slang::CompilerOptionName::VulkanUseEntryPointName, {slang::CompilerOptionValueKind::Int, 1}});
  // m_options.push_back({slang::CompilerOptionName::AllowGLSL, {slang::CompilerOptionValueKind::Int, 1}});
}

void nvslang::SlangCompiler::addSearchPaths(const std::vector<std::filesystem::path>& searchPaths)
{
  for(auto& str : searchPaths)
  {
    m_searchPaths.push_back(str);                             // For nvutils::findFile()
    m_searchPathsUtf8.push_back(nvutils::utf8FromPath(str));  // Need to keep the UTF-8 allocation alive
    // Slang expects const char* to UTF-8; see implementation of Slang's FileStream::_init().
    m_searchPathsUtf8Pointers.push_back(m_searchPathsUtf8.back().c_str());
  }
}

void nvslang::SlangCompiler::clearSearchPaths()
{
  m_searchPaths.clear();
  m_searchPathsUtf8.clear();
  m_searchPathsUtf8Pointers.clear();
}

const uint32_t* nvslang::SlangCompiler::getSpirv() const
{
  if(!m_spirv)
  {
    return nullptr;
  }
  return reinterpret_cast<const uint32_t*>(m_spirv->getBufferPointer());
}

size_t nvslang::SlangCompiler::getSpirvSize() const
{
  if(!m_spirv)
  {
    return 0;
  }
  return m_spirv->getBufferSize();
}

slang::IComponentType* nvslang::SlangCompiler::getSlangProgram() const
{
  if(!m_linkedProgram)
  {
    return nullptr;
  }
  return m_linkedProgram.get();
}

slang::IModule* nvslang::SlangCompiler::getSlangModule() const
{
  if(!m_module)
  {
    return nullptr;
  }
  return m_module.get();
}

bool nvslang::SlangCompiler::compileFile(const std::filesystem::path& filename)
{
  const std::filesystem::path sourceFile = nvutils::findFile(filename, m_searchPaths);
  if(sourceFile.empty())
  {
    m_lastDiagnosticMessage = "File not found: " + nvutils::utf8FromPath(filename);
    LOGW("%s\n", m_lastDiagnosticMessage.c_str());
    return false;
  }
  bool success = loadFromSourceString(nvutils::utf8FromPath(sourceFile.stem()), nvutils::utf8FromPath(sourceFile), nvutils::loadFile(sourceFile));
  if(success)
  {
    if(m_callback)
    {
      m_callback(sourceFile, getSpirv(), getSpirvSize());
    }
  }

  return success;
}

void nvslang::SlangCompiler::logAndAppendDiagnostics(slang::IBlob* diagnostics)
{
  if(diagnostics)
  {
    const char* message = reinterpret_cast<const char*>(diagnostics->getBufferPointer());
    // Since these are often multi-line, we want to print them with extra spaces:
    LOGW("\n%s\n", message);
    // Append onto m_lastDiagnosticMessage, separated by a newline:
    if(m_lastDiagnosticMessage.empty())
    {
      m_lastDiagnosticMessage += '\n';
    }
    m_lastDiagnosticMessage += message;
  }
}

bool nvslang::SlangCompiler::loadFromSourceString(const std::string& moduleName, const std::string& path, const std::string& slangSource)
{
  createSession();

  // Clear any previous compilation
  m_spirv = nullptr;
  m_lastDiagnosticMessage.clear();

  Slang::ComPtr<slang::IBlob> diagnostics;
  // From source code to Slang module
  m_module = m_session->loadModuleFromSourceString(moduleName.c_str(), path.c_str(), slangSource.c_str(), diagnostics.writeRef());
  logAndAppendDiagnostics(diagnostics);
  if(!m_module)
  {
    return false;
  }

  // In order to get entrypoint shader reflection, it seems like one must go
  // through the additional step of listing every entry point in the composite
  // type. This matches the docs, but @nbickford wonders if there's a simpler way.
  const SlangInt32                               definedEntryPointCount = m_module->getDefinedEntryPointCount();
  std::vector<Slang::ComPtr<slang::IEntryPoint>> entryPoints(definedEntryPointCount);
  std::vector<slang::IComponentType*>            components(1 + definedEntryPointCount);
  components[0] = m_module;
  for(SlangInt32 i = 0; i < definedEntryPointCount; i++)
  {
    m_module->getDefinedEntryPoint(i, entryPoints[i].writeRef());
    components[1 + i] = entryPoints[i];
  }

  Slang::ComPtr<slang::IComponentType> composedProgram;
  SlangResult result = m_session->createCompositeComponentType(components.data(), components.size(),
                                                               composedProgram.writeRef(), diagnostics.writeRef());
  logAndAppendDiagnostics(diagnostics);
  if(SLANG_FAILED(result) || !composedProgram)
  {
    return false;
  }

  // From composite component type to linked program
  result = composedProgram->link(m_linkedProgram.writeRef(), diagnostics.writeRef());
  logAndAppendDiagnostics(diagnostics);
  if(SLANG_FAILED(result) || !m_linkedProgram)
  {
    return false;
  }

  // From linked program to SPIR-V
  result = m_linkedProgram->getTargetCode(0, m_spirv.writeRef(), diagnostics.writeRef());
  logAndAppendDiagnostics(diagnostics);
  if(SLANG_FAILED(result) || nullptr == m_spirv)
  {
    return false;
  }
  return true;
}

void nvslang::SlangCompiler::createSession()
{
  m_session = {};

  if (!m_globalSession) {
      LOGE("Cannot create local session: m_globalSession is NULL!");
      return; // Stop the crash!
  }

#ifdef __ANDROID__
  // Initialize the Android file system if we haven't already
  if (!m_fileSystem) {
      m_fileSystem = new AndroidSlangFileSystem(g_assetManager);
  }

  m_globalSession->setDownstreamCompilerPath(SLANG_PASS_THROUGH_GLSLANG, "libslang-glslang-2026.3.1.so");
#endif

  slang::SessionDesc desc{
      .targets                  = m_targets.data(),
      .targetCount              = SlangInt(m_targets.size()),
      .searchPaths              = m_searchPathsUtf8Pointers.data(),
      .searchPathCount          = SlangInt(m_searchPathsUtf8Pointers.size()),
      .preprocessorMacros       = m_macros.data(),
      .preprocessorMacroCount   = SlangInt(m_macros.size()),
      .fileSystem               = m_fileSystem,
      .allowGLSLSyntax          = true,
      .compilerOptionEntries    = m_options.data(),
      .compilerOptionEntryCount = uint32_t(m_options.size()),
  };
  m_globalSession->createSession(desc, m_session.writeRef());
}

//--------------------------------------------------------------------------------------------------
// Usage example
//--------------------------------------------------------------------------------------------------
[[maybe_unused]] static void usage_SlangCompiler()
{
  nvslang::SlangCompiler slangCompiler;
  slangCompiler.defaultTarget();
  slangCompiler.defaultOptions();

  // Configure compiler settings as you wish
  const std::vector<std::filesystem::path> shadersPaths = {"include/shaders"};
  slangCompiler.addSearchPaths(shadersPaths);
  slangCompiler.addOption({slang::CompilerOptionName::DebugInformation,
                           {slang::CompilerOptionValueKind::Int, SLANG_DEBUG_INFO_LEVEL_MAXIMAL}});
  slangCompiler.addMacro({"MY_DEFINE", "1"});

  // Compile a shader file
  bool success = slangCompiler.compileFile("shader.slang");

  // Check if compilation was successful
  if(!success)
  {
    // Get the error message
    const std::string& errorMessages = slangCompiler.getLastDiagnosticMessage();
    LOGE("Compilation failed: %s\n", errorMessages.c_str());
  }
  else
  {
    // Get the compiled SPIR-V code
    const uint32_t* spirv     = slangCompiler.getSpirv();
    size_t          spirvSize = slangCompiler.getSpirvSize();

    // Check if there were any warnings
    const std::string& warningMessages = slangCompiler.getLastDiagnosticMessage();
    if(!warningMessages.empty())
    {
      LOGW("Compilation succeeded with warnings: %s\n", warningMessages.c_str());
    }
  }
}
