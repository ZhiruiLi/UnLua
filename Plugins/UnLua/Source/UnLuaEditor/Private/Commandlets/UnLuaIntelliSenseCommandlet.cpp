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

#include "Commandlets/UnLuaIntelliSenseCommandlet.h"

#include "Binding.h"
#include "Misc/FileHelper.h"
#include "UnLuaIntelliSenseGenerator.h"

namespace
{
/** 将静态导出类/枚举生成内容挂到 UE 命名空间，与运行时 UE.FImGui 等一致。 */
void WrapClassContentUnderUE(FString& Content, const FString& ClassName)
{
    Content = FString(TEXT("if not _G.UE then _G.UE = {} end\r\n")) + Content;
    Content.ReplaceInline(TEXT("local M = {}"), *FString::Printf(TEXT("UE.%s = {}"), *ClassName));
    Content.ReplaceInline(*FString::Printf(TEXT("function %s."), *ClassName), *FString::Printf(TEXT("function UE.%s."), *ClassName));
    Content.ReplaceInline(TEXT("\r\n\r\nreturn M\r\n"), TEXT(""));
    Content.ReplaceInline(TEXT("\r\nreturn M\r\n"), TEXT(""));
}

void WrapEnumContentUnderUE(FString& Content, const FString& EnumName)
{
    Content = FString(TEXT("if not _G.UE then _G.UE = {} end\r\n")) + Content;
    Content.ReplaceInline(TEXT("local M = {}"), *FString::Printf(TEXT("UE.%s = {}"), *EnumName));
    Content.ReplaceInline(TEXT("\r\n\r\nreturn M\r\n"), TEXT(""));
    Content.ReplaceInline(TEXT("\r\nreturn M\r\n"), TEXT(""));
}
} // namespace

UUnLuaIntelliSenseCommandlet::UUnLuaIntelliSenseCommandlet(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    //IntermediateDir = FPaths::ProjectIntermediateDir();
    IntermediateDir = FPaths::ProjectPluginsDir();
    IntermediateDir += TEXT("UnLua/Intermediate/");
}

int32 UUnLuaIntelliSenseCommandlet::Main(const FString &Params)
{
    // class black list
    TArray<FString> ClassBlackList;
    ClassBlackList.Add("int8");
    ClassBlackList.Add("int16");
    ClassBlackList.Add("int32");
    ClassBlackList.Add("int64");
    ClassBlackList.Add("uint8");
    ClassBlackList.Add("uint16");
    ClassBlackList.Add("uint32");
    ClassBlackList.Add("uint64");
    ClassBlackList.Add("float");
    ClassBlackList.Add("double");
    ClassBlackList.Add("bool");
    ClassBlackList.Add("FName");
    ClassBlackList.Add("FString");
    ClassBlackList.Add("FText");

    // enum black list
    TArray<FString> EnumBlackList;

    // function black list
    TArray<FString> FuncBlackList;
    FuncBlackList.Add("OnModuleHotfixed");

    const auto ExportedReflectedClasses = UnLua::GetExportedReflectedClasses();
    const auto ExportedNonReflectedClasses = UnLua::GetExportedNonReflectedClasses();
    const auto ExportedEnums = UnLua::GetExportedEnums();
    const auto ExportedFunctions = UnLua::GetExportedFunctions();
    
    FString GeneratedFileContent;
    FString ModuleName(TEXT("StaticallyExports"));
    
    // reflected classes
    for (auto Pair : ExportedReflectedClasses)
    {
        if (ClassBlackList.Contains(Pair.Key))
            continue;

        Pair.Value->GenerateIntelliSense(GeneratedFileContent);
        WrapClassContentUnderUE(GeneratedFileContent, Pair.Key);
        SaveFile(ModuleName, Pair.Key, GeneratedFileContent);
    }

    for (auto Pair : ExportedNonReflectedClasses)
    {
        if (ClassBlackList.Contains(Pair.Key))
            continue;

        Pair.Value->GenerateIntelliSense(GeneratedFileContent);
        WrapClassContentUnderUE(GeneratedFileContent, Pair.Key);
        SaveFile(ModuleName, Pair.Key, GeneratedFileContent);
    }

    for (auto Enum : ExportedEnums)
    {
        if (EnumBlackList.Contains(Enum->GetName()))
            continue;

        Enum->GenerateIntelliSense(GeneratedFileContent);
        WrapEnumContentUnderUE(GeneratedFileContent, Enum->GetName());
        SaveFile(ModuleName, Enum->GetName(), GeneratedFileContent);
    }

    for (auto Function : ExportedFunctions)
    {
        if (FuncBlackList.Contains(Function->GetName()))
            continue;
    
        Function->GenerateIntelliSense(GeneratedFileContent);
    }
    
    SaveFile(ModuleName, TEXT("GlobalFunctions"), GeneratedFileContent);

    // generate blueprint intellisense if needed
    TArray<FString> Tokens;
    TArray<FString> Switches;
    TMap<FString, FString> ParamsMap;
    ParseCommandLine(*Params, Tokens, Switches, ParamsMap);
    FString BPKey = TEXT("BP");
    if (ParamsMap.Contains(BPKey) && ParamsMap[BPKey] == TEXT("1"))
    {
        auto Generator = FUnLuaIntelliSenseGenerator::Get();
        Generator->Initialize();
        Generator->UpdateAll();
    }

    return 0;
}

void UUnLuaIntelliSenseCommandlet::SaveFile(const FString &ModuleName, const FString &FileName, const FString &GeneratedFileContent)
{
    IFileManager &FileManager = IFileManager::Get();
    FString Directory = FString::Printf(TEXT("%sIntelliSense/%s"), *IntermediateDir, *ModuleName);
    if (!FileManager.DirectoryExists(*Directory))
    {
        FileManager.MakeDirectory(*Directory);
    }

    const FString FilePath = FString::Printf(TEXT("%s/%s.lua"), *Directory, *FileName);
    FString FileContent;
    FFileHelper::LoadFileToString(FileContent, *FilePath);
    if (FileContent != GeneratedFileContent)
    {
        bool bResult = FFileHelper::SaveStringToFile(GeneratedFileContent, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        check(bResult);
    }
}
