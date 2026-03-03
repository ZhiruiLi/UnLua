// Tencent is pleased to support the open source community by making UnLua available.
// 
// Copyright (C) 2019 Tencent. All rights reserved.
//
// Licensed under the MIT License (the "License"); 
// you may not use this file except in compliance with the License. You may obtain a copy of the License at
//
// http://opensource.org/licenses/MIT
//
// Unless required by applicable law or agreed to in writing, 
// software distributed under the License is distributed on an "AS IS" BASIS, 
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
// See the License for the specific language governing permissions and limitations under the License.

#pragma once

#include "CoreUObject.h"

class UBlueprint;
class UWidgetBlueprint;

class FUnLuaIntelliSenseGenerator
{
public:
    FUnLuaIntelliSenseGenerator()
        : bInitialized(false)
    {
    }

    static TSharedRef<FUnLuaIntelliSenseGenerator> Get();
    
    // Update all Blueprints from command
    void UpdateAll();

    void Initialize();

private:
    static TSharedPtr<FUnLuaIntelliSenseGenerator> Singleton;

    static bool IsBlueprint(const FAssetData& AssetData);

    static bool ShouldExport(const FAssetData& AssetData, bool bLoad = false);

    /** 仅根据 Blueprint 对象与设置判断是否导出（不依赖 FAssetData，用于按类型导出分支）。 */
    static bool ShouldExportBlueprint(const UBlueprint* Blueprint);

    void Export(const UBlueprint* Blueprint);

    void Export(const UField* Field);

    void ExportUE(const TArray<const UField*>& Types);
    /** 生成 UE.lua 并追加 AdditionalTypeNames 的 ---@type 与 = nil 条目（用于静态导出类/枚举）。 */
    void ExportUE(const TArray<const UField*>& Types, const TArray<FString>& AdditionalTypeNames);

    /** 静态导出的类与枚举（如 LuaCogImGui 的 FImGui、ImVec2）写入 StaticallyExports 目录。ExcludedTypeNames 中已有的类型（如已在原生路径导出的 FVector）不再重复导出。 */
    void ExportStaticallyExportedClassesAndEnums(const TSet<FString>& ExcludedTypeNames);

    void ExportGlobalFunctions();

    void ExportUnLua();

    void CollectTypes(TArray<const UField*> &Types);
    
    // File helper
    void SaveFile(const FString& ModuleName, const FString& FileName, const FString& GeneratedFileContent);
    void DeleteFile(const FString& ModuleName, const FString& FileName);

    // Handle asset event
    void OnAssetAdded(const FAssetData& AssetData);
    void OnAssetRemoved(const FAssetData& AssetData);
    void OnAssetRenamed(const FAssetData& AssetData, const FString& OldPath);
    void OnAssetUpdated(const FAssetData& AssetData);

    FString OutputDir;
    bool bInitialized;
};
