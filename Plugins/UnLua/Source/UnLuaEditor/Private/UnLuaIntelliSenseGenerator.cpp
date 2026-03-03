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

#include "Misc/EngineVersionComparison.h"
#include "UnLuaIntelliSenseGenerator.h"
#if UE_VERSION_NEWER_THAN(5, 1, 0)
#include "AssetRegistry/AssetRegistryModule.h"
#else
#include "AssetRegistryModule.h"
#endif
#include "CoreUObject.h"
#include "UObject/SoftObjectPath.h"
#include "UnLua.h"
#include "UnLuaEditorSettings.h"
#include "Binding.h"
#include "UnLuaIntelliSense.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Blueprint.h"
#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "UnLuaIntelliSenseGenerator"

TSharedPtr<FUnLuaIntelliSenseGenerator> FUnLuaIntelliSenseGenerator::Singleton;

/** 收集静态导出类/枚举的类型名（与 ExportStaticallyExportedClassesAndEnums 的名单一致），用于写入 UE.lua。ExcludedTypeNames 中已有的类型不加入，避免与原生导出重复。 */
static void CollectStaticallyExportedTypeNames(TArray<FString>& OutNames, const TSet<FString>& ExcludedTypeNames)
{
#if WITH_EDITOR
	static const TArray<FString> ClassBlackList = {
		TEXT("int8"), TEXT("int16"), TEXT("int32"), TEXT("int64"),
		TEXT("uint8"), TEXT("uint16"), TEXT("uint32"), TEXT("uint64"),
		TEXT("float"), TEXT("double"), TEXT("bool"), TEXT("FName"), TEXT("FString"),
		TEXT("TArray"), TEXT("TMap"), TEXT("TSet"),  // 已由默认反射导出，不再在 StaticallyExports 中重复
	};
	for (const auto& Pair : UnLua::GetExportedReflectedClasses())
	{
		if (!ClassBlackList.Contains(Pair.Key) && !ExcludedTypeNames.Contains(Pair.Key))
			OutNames.Add(Pair.Key);
	}
	for (const auto& Pair : UnLua::GetExportedNonReflectedClasses())
	{
		if (!ClassBlackList.Contains(Pair.Key) && !ExcludedTypeNames.Contains(Pair.Key))
			OutNames.Add(Pair.Key);
	}
	for (UnLua::IExportedEnum* Enum : UnLua::GetExportedEnums())
	{
		if (!ExcludedTypeNames.Contains(Enum->GetName()))
			OutNames.Add(Enum->GetName());
	}
#endif
}

TSharedRef<FUnLuaIntelliSenseGenerator> FUnLuaIntelliSenseGenerator::Get()
{
    if (!Singleton.IsValid())
        Singleton = MakeShareable(new FUnLuaIntelliSenseGenerator);
    return Singleton.ToSharedRef();
}

void FUnLuaIntelliSenseGenerator::Initialize()
{
    if (bInitialized)
        return;

    OutputDir = IPluginManager::Get().FindPlugin("UnLua")->GetBaseDir() + "/Intermediate/IntelliSense";

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    AssetRegistryModule.Get().OnAssetAdded().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetAdded);
    AssetRegistryModule.Get().OnAssetRemoved().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetRemoved);
    AssetRegistryModule.Get().OnAssetRenamed().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetRenamed);
    AssetRegistryModule.Get().OnAssetUpdated().AddRaw(this, &FUnLuaIntelliSenseGenerator::OnAssetUpdated);

    bInitialized = true;
}

void FUnLuaIntelliSenseGenerator::UpdateAll()
{
    const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

    FARFilter Filter;
#if UE_VERSION_OLDER_THAN(5, 1, 0)
    Filter.ClassNames.Add(UBlueprint::StaticClass()->GetFName());
    Filter.ClassNames.Add(UWidgetBlueprint::StaticClass()->GetFName());
#else
    Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
    Filter.ClassPaths.Add(UWidgetBlueprint::StaticClass()->GetClassPathName());
#endif

    TArray<FAssetData> BlueprintAssets;
    TArray<const UField*> NativeTypes;
    AssetRegistryModule.Get().GetAssets(Filter, BlueprintAssets);
    CollectTypes(NativeTypes);

    auto TotalCount = BlueprintAssets.Num() + NativeTypes.Num();
    if (TotalCount == 0)
        return;

    TotalCount++;

    FScopedSlowTask SlowTask(TotalCount, LOCTEXT("GeneratingBlueprintsIntelliSense", "Generating Blueprints InstelliSense"));
    SlowTask.MakeDialog();

    for (int32 i = 0; i < BlueprintAssets.Num(); i++)
    {
        if (SlowTask.ShouldCancel())
            break;
        OnAssetUpdated(BlueprintAssets[i]);
        SlowTask.EnterProgressFrame();
    }

    // 已被默认/反射导出的类型不再写入 Script/ModuleName/ 下的单文件，仅保留在 UE.lua 中的声明
    static const TSet<FString> NativeExportSkipList = { TEXT("UClass") };
    for (const auto Type : NativeTypes)
    {
        if (SlowTask.ShouldCancel())
            break;
        if (NativeExportSkipList.Contains(UnLua::IntelliSense::GetTypeName(Type)))
        {
            SlowTask.EnterProgressFrame();
            continue;
        }
        Export(Type);
        SlowTask.EnterProgressFrame();
    }

    if (SlowTask.ShouldCancel())
        return;
    TSet<FString> NativeTypeNames;
    for (const UField* Type : NativeTypes)
        NativeTypeNames.Add(UnLua::IntelliSense::GetTypeName(Type));
    TArray<FString> StaticTypeNames;
    CollectStaticallyExportedTypeNames(StaticTypeNames, NativeTypeNames);
    ExportUE(NativeTypes, StaticTypeNames);
    ExportStaticallyExportedClassesAndEnums(NativeTypeNames);
    ExportGlobalFunctions();
    ExportUnLua();
    SlowTask.EnterProgressFrame();
}

bool FUnLuaIntelliSenseGenerator::IsBlueprint(const FAssetData& AssetData)
{
#if UE_VERSION_OLDER_THAN(5, 1, 0)
    const FName AssetClass = AssetData.AssetClass;
    return AssetClass == UBlueprint::StaticClass()->GetFName() || AssetClass == UWidgetBlueprint::StaticClass()->GetFName();
#else
    // UE5.1+ 使用 FTopLevelAssetPath 比较，GetName() 仅短名会与 AssetClassPath.ToString() 不一致
    return AssetData.AssetClassPath == UBlueprint::StaticClass()->GetClassPathName()
        || AssetData.AssetClassPath == UWidgetBlueprint::StaticClass()->GetClassPathName();
#endif
}

bool FUnLuaIntelliSenseGenerator::ShouldExport(const FAssetData& AssetData, bool bLoad)
{
    const auto& Settings = *GetDefault<UUnLuaEditorSettings>();
    if (!Settings.bGenerateIntelliSense)
        return false;

    if (!IsBlueprint(AssetData))
        return false;

    // 先用路径过滤，避免对大量资源执行 FastGetAsset(true) 导致全量加载、导出极慢
    const FString Path = AssetData.GetObjectPathString();
    bool bPathMatch = false;
    for (const FString& ExportPath : Settings.ExportPaths)
    {
        if (Path.StartsWith(ExportPath))
        {
            bPathMatch = true;
            break;
        }
    }
    if (!bPathMatch)
    {
        return false;
    }

    const auto Asset = AssetData.FastGetAsset(bLoad);
    if (!Asset)
        return false;

    const auto Blueprint = Cast<UBlueprint>(Asset);
    if (!Blueprint)
        return false;

    if (Blueprint->SkeletonGeneratedClass || Blueprint->GeneratedClass)
        return true;

    return false;
}

void FUnLuaIntelliSenseGenerator::Export(const UBlueprint* Blueprint)
{
    Export(Blueprint->GeneratedClass);
}

void FUnLuaIntelliSenseGenerator::Export(const UField* Field)
{
#if ENGINE_MAJOR_VERSION > 4 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 26)
    const UPackage* Package = Field->GetPackage();
#else
    const UPackage* Package = (UPackage*)Field->GetTypedOuter(UPackage::StaticClass());
#endif
    auto ModuleName = Package->GetName();
    if (!Field->IsNative())
    {
        int32 LastSlashIndex;
        if (ModuleName.FindLastChar('/', LastSlashIndex))
            ModuleName.LeftInline(LastSlashIndex);
    }
    FString FileName = UnLua::IntelliSense::GetTypeName(Field);
    if (FileName.EndsWith("_C"))
        FileName.LeftChopInline(2);
    const FString Content = UnLua::IntelliSense::Get(Field);
    SaveFile(ModuleName, FileName, Content);
}

void FUnLuaIntelliSenseGenerator::ExportUE(const TArray<const UField*>& Types)
{
    ExportUE(Types, TArray<FString>());
}

void FUnLuaIntelliSenseGenerator::ExportUE(const TArray<const UField*>& Types, const TArray<FString>& AdditionalTypeNames)
{
    FString Content = UnLua::IntelliSense::GetUE(Types);
    if (AdditionalTypeNames.Num() > 0)
    {
        // GetUE 以 "}\r\n" 结尾，在闭合括号前插入静态类型的 ---@type 与 = nil
        if (Content.EndsWith(TEXT("}\r\n")))
        {
            Content.LeftChopInline(3);
            for (const FString& Name : AdditionalTypeNames)
                Content += FString::Printf(TEXT("\r\n    ---@type %s\r\n    %s = nil,\r\n"), *Name, *Name);
            Content += TEXT("}\r\n");
        }
    }
    SaveFile("", "UE", Content);
}

/** 将静态导出生成的 “local M = {}; return M” 改为 “local TypeName = {}; return TypeName”，与原生 UObject.lua 一致，便于 IDE 解析定义位置。 */
static void WrapStaticallyExportedContentAsLocal(FString& Content, const FString& TypeName)
{
    Content.ReplaceInline(TEXT("local M = {}"), *FString::Printf(TEXT("local %s = {}"), *TypeName));
    Content.ReplaceInline(TEXT("\r\n\r\nreturn M\r\n"), *FString::Printf(TEXT("\r\n\r\nreturn %s\r\n"), *TypeName));
    Content.ReplaceInline(TEXT("\r\nreturn M\r\n"), *FString::Printf(TEXT("\r\nreturn %s\r\n"), *TypeName));
}

void FUnLuaIntelliSenseGenerator::ExportStaticallyExportedClassesAndEnums(const TSet<FString>& ExcludedTypeNames)
{
#if WITH_EDITOR
    static const TArray<FString> ClassBlackList = {
        TEXT("int8"), TEXT("int16"), TEXT("int32"), TEXT("int64"),
        TEXT("uint8"), TEXT("uint16"), TEXT("uint32"), TEXT("uint64"),
        TEXT("float"), TEXT("double"), TEXT("bool"), TEXT("FName"), TEXT("FString"),
        TEXT("TArray"), TEXT("TMap"), TEXT("TSet"),  // 已由默认反射导出，不再在 StaticallyExports 中重复
    };
    const FString ModuleName = TEXT("StaticallyExports");

    const TMap<FString, UnLua::IExportedClass*>& Reflected = UnLua::GetExportedReflectedClasses();
    for (const auto& Pair : Reflected)
    {
        if (ClassBlackList.Contains(Pair.Key) || ExcludedTypeNames.Contains(Pair.Key))
            continue;
        FString Content;
        Pair.Value->GenerateIntelliSense(Content);
        WrapStaticallyExportedContentAsLocal(Content, Pair.Key);
        SaveFile(ModuleName, Pair.Key, Content);
    }

    const TMap<FString, UnLua::IExportedClass*>& NonReflected = UnLua::GetExportedNonReflectedClasses();
    for (const auto& Pair : NonReflected)
    {
        if (ClassBlackList.Contains(Pair.Key) || ExcludedTypeNames.Contains(Pair.Key))
            continue;
        FString Content;
        Pair.Value->GenerateIntelliSense(Content);
        WrapStaticallyExportedContentAsLocal(Content, Pair.Key);
        SaveFile(ModuleName, Pair.Key, Content);
    }

    const TArray<UnLua::IExportedEnum*>& Enums = UnLua::GetExportedEnums();
    for (UnLua::IExportedEnum* Enum : Enums)
    {
        if (ExcludedTypeNames.Contains(Enum->GetName()))
            continue;
        FString Content;
        Enum->GenerateIntelliSense(Content);
        WrapStaticallyExportedContentAsLocal(Content, Enum->GetName());
        SaveFile(ModuleName, Enum->GetName(), Content);
    }
#endif
}

void FUnLuaIntelliSenseGenerator::ExportGlobalFunctions()
{
#if WITH_EDITOR
    static const TArray<FString> FuncBlackList = { TEXT("OnModuleHotfixed") };
    const TArray<UnLua::IExportedFunction*>& ExportedFunctions = UnLua::GetExportedFunctions();
    FString GeneratedFileContent;
    for (UnLua::IExportedFunction* Function : ExportedFunctions)
    {
        if (FuncBlackList.Contains(Function->GetName()))
            continue;
        Function->GenerateIntelliSense(GeneratedFileContent);
    }
    if (!GeneratedFileContent.IsEmpty())
    {
        SaveFile(TEXT("StaticallyExports"), TEXT("GlobalFunctions"), GeneratedFileContent);
    }
#endif
}

void FUnLuaIntelliSenseGenerator::ExportUnLua()
{
    const auto ContentDir = IPluginManager::Get().FindPlugin(TEXT("UnLua"))->GetContentDir();
    const auto SrcDir = ContentDir / "IntelliSense";
    const auto DstDir = OutputDir;

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*SrcDir))
        return;

    PlatformFile.CopyDirectoryTree(*DstDir, *SrcDir, true);
}

void FUnLuaIntelliSenseGenerator::CollectTypes(TArray<const UField*>& Types)
{
    for (TObjectIterator<UClass> It; It; ++It)
    {
        const UClass* Class = *It;
        // 不收集蓝图生成的类，蓝图 IntelliSense 仅由按资源路径导出的分支（OnAssetUpdated）负责
        if (Class->ClassGeneratedBy && Class->ClassGeneratedBy->IsA<UBlueprint>())
        {
            continue;
        }
        const FString ClassName = Class->GetName();
        // ReSharper disable StringLiteralTypo
        if (ClassName.StartsWith("SKEL_")
            || ClassName.StartsWith("PLACEHOLDER-CLASS")
            || ClassName.StartsWith("REINST_")
            || ClassName.StartsWith("TRASHCLASS_")
            || ClassName.StartsWith("HOTRELOADED_")
            || ClassName.Contains(TEXT("__PythonCallable"))  // 编辑器/自动化内部类型，不参与 Lua IntelliSense
        )
        {
            // skip nonsense types
            continue;
        }
        // ReSharper restore StringLiteralTypo
        Types.Add(Class);
    }

    for (TObjectIterator<UScriptStruct> It; It; ++It)
    {
        const UScriptStruct* ScriptStruct = *It;
        if (ScriptStruct->GetName().Contains(TEXT("__PythonCallable")))
            continue;
        Types.Add(ScriptStruct);
    }

    for (TObjectIterator<UEnum> It; It; ++It)
    {
        const UEnum* Enum = *It;
        if (Enum->GetName().Contains(TEXT("__PythonCallable")))
            continue;
        Types.Add(Enum);
    }
}

void FUnLuaIntelliSenseGenerator::SaveFile(const FString& ModuleName, const FString& FileName, const FString& GeneratedFileContent)
{
    IFileManager& FileManager = IFileManager::Get();
    const FString Directory = OutputDir / ModuleName;
    if (!FileManager.DirectoryExists(*Directory))
        FileManager.MakeDirectory(*Directory);

    const FString FilePath = FString::Printf(TEXT("%s/%s.lua"), *Directory, *FileName);
    FString FileContent;
    FFileHelper::LoadFileToString(FileContent, *FilePath);
    if (FileContent != GeneratedFileContent)
        FFileHelper::SaveStringToFile(GeneratedFileContent, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void FUnLuaIntelliSenseGenerator::DeleteFile(const FString& ModuleName, const FString& FileName)
{
    IFileManager& FileManager = IFileManager::Get();
    const FString Directory = OutputDir / ModuleName;
    if (!FileManager.DirectoryExists(*Directory))
        FileManager.MakeDirectory(*Directory);

    const FString FilePath = FString::Printf(TEXT("%s/%s.lua"), *Directory, *FileName);
    if (FileManager.FileExists(*FilePath))
        FileManager.Delete(*FilePath);
}

void FUnLuaIntelliSenseGenerator::OnAssetAdded(const FAssetData& AssetData)
{
    if (!ShouldExport(AssetData))
        return;

    OnAssetUpdated(AssetData);

    TArray<const UField*> Types;
    CollectTypes(Types);
    ExportUE(Types);
}

void FUnLuaIntelliSenseGenerator::OnAssetRemoved(const FAssetData& AssetData)
{
    if (!ShouldExport(AssetData))
        return;

    DeleteFile(FString("/Game"), AssetData.AssetName.ToString());
}

void FUnLuaIntelliSenseGenerator::OnAssetRenamed(const FAssetData& AssetData, const FString& OldPath)
{
    if (!ShouldExport(AssetData))
        return;

    //remove old Blueprint name
    const FString OldPackageName = FPackageName::GetShortName(OldPath);
    DeleteFile("/Game", OldPackageName);

    //update new name 
    OnAssetUpdated(AssetData);
}

void FUnLuaIntelliSenseGenerator::OnAssetUpdated(const FAssetData& AssetData)
{
    if (!ShouldExport(AssetData, true))
        return;

    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetData.GetSoftObjectPath().ToString());
    if (!Blueprint)
        return;

    Export(Blueprint);
}
