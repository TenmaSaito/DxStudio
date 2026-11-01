//==================================================================================
//
// モデルの変換（X ⇔ マテリアル / FBX → X）のソースファイル [ModelConvert.cpp]
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "ModelConvert.h"

#include "Engine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <system_error>

#include "../third_party/ufbx/ufbx.h"

namespace dxs {
namespace {

//**********************************************************************************
// *** 定数宣言 ***
//**********************************************************************************
constexpr std::size_t MAX_FILE_BYTES = 64u * 1024u * 1024u; // 読み込む X / FBX の最大サイズ
constexpr std::size_t MAX_VERTICES = 1000000;               // エンジンの X ファイル読み込みの上限に合わせる

//**********************************************************************************
// *** 文字列の補助関数 ***
//**********************************************************************************
// --- パスを UTF-8 の文字列にする処理 ---
std::string PathUtf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

// --- 識別子に使える文字か（英数字と _） ---
bool IsIdentifierChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

// --- 大文字小文字を区別しない検索処理 ---
std::size_t FindNoCase(const std::string& sText, const std::string& sWord, std::size_t nFrom, std::size_t nEnd) {
    if (sWord.empty() || nEnd < sWord.size()) {
        return std::string::npos;
    }

    for (std::size_t nPos = nFrom; nPos + sWord.size() <= nEnd; ++nPos) {
        bool bMatch = true;
        for (std::size_t nChar = 0; nChar < sWord.size(); ++nChar) {
            if (std::tolower(static_cast<unsigned char>(sText[nPos + nChar])) !=
                std::tolower(static_cast<unsigned char>(sWord[nChar]))) {
                bMatch = false;
                break;
            }
        }

        if (bMatch) {
            return nPos;
        }
    }
    return std::string::npos;
}

// --- ファイル名に使えない文字を _ に置き換える処理 ---
std::wstring SafeFileStem(const std::string& sName) {
    std::wstring sResult;
    for (const char c : sName) {
        if (IsIdentifierChar(c) || c == '-') {
            sResult += static_cast<wchar_t>(c);
        } else {
            sResult += L'_';
        }
    }
    return sResult;
}

// --- X ファイルの名前（識別子）に使える形にする処理 ---
std::string XIdentifier(const std::string& sName, std::size_t nIndex) {
    std::string sResult;
    for (const char c : sName) {
        sResult += IsIdentifierChar(c) ? c : '_';
    }

    // 空、または数字で始まる名前は使えないので接頭辞を付ける
    if (sResult.empty() || std::isdigit(static_cast<unsigned char>(sResult.front())) != 0) {
        sResult = "Material_" + std::to_string(nIndex) + (sResult.empty() ? "" : "_" + sResult);
    }
    return sResult;
}

// --- ファイル全体の読み込み処理 ---
bool ReadWholeFile(const std::filesystem::path& file, std::string& sText, std::string& error) {
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        error = "Cannot open the file: " + PathUtf8(file);
        return false;
    }

    sText.assign(std::istreambuf_iterator<char>(input), {});
    if (sText.empty() || sText.size() > MAX_FILE_BYTES) {
        error = "The file is empty or too large: " + PathUtf8(file);
        return false;
    }
    return true;
}

// --- 一時ファイルに書いてから置き換える処理（書き込み途中で壊れないように） ---
bool WriteFileAtomically(const std::filesystem::path& file, const std::string& sText, std::string& error) {
    auto temp = file;
    temp += L".tmp";

    {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "Cannot write the file: " + PathUtf8(temp);
            return false;
        }

        output << sText;
        output.close();
        if (!output) {
            error = "Failed to write the file: " + PathUtf8(temp);
            return false;
        }
    }

    if (!MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "Cannot replace the file (Windows error " + std::to_string(GetLastError()) + "): " + PathUtf8(file);
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        return false;
    }
    return true;
}

//**********************************************************************************
// *** X ファイルの Material の読み取り ***
//**********************************************************************************
// --- 位置 nOpen の '{' に対応する '}' を探す処理（文字列の中の括弧は数えない） ---
std::size_t MatchingBrace(const std::string& sText, std::size_t nOpen) {
    int nDepth = 0;
    bool bInString = false;
    for (std::size_t nPos = nOpen; nPos < sText.size(); ++nPos) {
        const char c = sText[nPos];
        if (c == '"') {
            bInString = !bInString;
            continue;
        }
        if (bInString) {
            continue;
        }

        if (c == '{') {
            ++nDepth;
        } else if (c == '}') {
            --nDepth;
            if (nDepth == 0) {
                return nPos;
            }
        }
    }
    return std::string::npos;
}

// --- 範囲 [nBegin, nEnd) から数値を順に nCount 個読む処理 ---
bool ScanFloats(const std::string& sText, std::size_t nBegin, std::size_t nEnd, float* pValues, int nCount) {
    const char* p = sText.data() + nBegin;
    const char* const pEnd = sText.data() + nEnd;
    int nRead = 0;

    while (p < pEnd && nRead < nCount) {
        // 数値の始まりまで読み飛ばす（; や , などの区切り文字）
        const bool bNumberStart = (*p >= '0' && *p <= '9') || *p == '-' || *p == '+' || *p == '.';
        if (!bNumberStart) {
            ++p;
            continue;
        }

        char* pNext = nullptr;
        const double dValue = std::strtod(p, &pNext);
        if (pNext == p) {
            ++p; // "-" だけなど、数値として読めなかった
            continue;
        }

        pValues[nRead] = static_cast<float>(dValue);
        ++nRead;
        p = pNext;
    }
    return nRead == nCount;
}

// --- 直前の単語が "template" か（テンプレートの宣言は読み飛ばす） ---
bool IsTemplateDeclaration(const std::string& sText, std::size_t nWord) {
    std::size_t nPos = nWord;
    while (nPos > 0 && std::isspace(static_cast<unsigned char>(sText[nPos - 1])) != 0) {
        --nPos;
    }

    constexpr std::string_view TEMPLATE = "template";
    return nPos >= TEMPLATE.size() && sText.compare(nPos - TEMPLATE.size(), TEMPLATE.size(), TEMPLATE) == 0;
}

// --- 位置 nWord の "Material" から 1 つ分を読む処理（読めなければ false） ---
bool ParseMaterialAt(const std::string& sText, std::size_t nWord, XMaterialInfo& material, std::size_t& nNext) {
    constexpr std::string_view WORD = "Material";
    std::size_t nPos = nWord + WORD.size();

    // "Material" の後: 空白、名前（任意）、空白、'{'
    while (nPos < sText.size() && std::isspace(static_cast<unsigned char>(sText[nPos])) != 0) {
        ++nPos;
    }
    const std::size_t nNameBegin = nPos;
    while (nPos < sText.size() && (IsIdentifierChar(sText[nPos]) || sText[nPos] == '-' || sText[nPos] == '.')) {
        ++nPos;
    }
    material.sName = sText.substr(nNameBegin, nPos - nNameBegin);
    while (nPos < sText.size() && std::isspace(static_cast<unsigned char>(sText[nPos])) != 0) {
        ++nPos;
    }
    if (nPos >= sText.size() || sText[nPos] != '{') {
        return false;
    }

    const std::size_t nOpen = nPos;
    const std::size_t nClose = MatchingBrace(sText, nOpen);
    if (nClose == std::string::npos) {
        return false;
    }
    nNext = nClose + 1;

    // 数値はテクスチャの指定より前にある: 拡散色 4、強さ 1、鏡面反射色 3、発光色 3
    const std::size_t nTexture = FindNoCase(sText, "TextureFilename", nOpen, nClose);
    const std::size_t nNumbersEnd = nTexture == std::string::npos ? nClose : nTexture;
    float aValues[11]{};
    if (!ScanFloats(sText, nOpen + 1, nNumbersEnd, aValues, 11)) {
        return false;
    }

    material.diffuse = {aValues[0], aValues[1], aValues[2], aValues[3]};
    material.fPower = aValues[4];
    material.specular = {aValues[5], aValues[6], aValues[7]};
    material.emissive = {aValues[8], aValues[9], aValues[10]};

    // TextureFilename { "ファイル名"; }
    if (nTexture != std::string::npos) {
        const std::size_t nQuote = sText.find('"', nTexture);
        const std::size_t nQuoteEnd = nQuote == std::string::npos ? nQuote : sText.find('"', nQuote + 1);
        if (nQuoteEnd != std::string::npos && nQuoteEnd < nClose) {
            material.sTexture = sText.substr(nQuote + 1, nQuoteEnd - nQuote - 1);
            std::replace(material.sTexture.begin(), material.sTexture.end(), '\\', '/');

            // X ファイルの文字列では \\ が \ を表すため、続いた / を 1 つにまとめる
            const auto newEnd = std::unique(material.sTexture.begin(), material.sTexture.end(),
                                            [](char a, char b) { return a == '/' && b == '/'; });
            material.sTexture.erase(newEnd, material.sTexture.end());
        }
    }

    // 色は有限の値であること
    const float aCheck[] = {material.diffuse.x, material.diffuse.y, material.diffuse.z, material.diffuse.w};
    for (const float fValue : aCheck) {
        if (!std::isfinite(fValue)) {
            return false;
        }
    }
    return true;
}

//**********************************************************************************
// *** X ファイルの書き出し ***
//**********************************************************************************
// X ファイルに書く頂点 1 つ（三角形の角ごとに持つので、法線と UV も一緒に持てる）
struct XVertex {
    ufbx_vec3 position{};
    ufbx_vec3 normal{};
    ufbx_vec2 uv{};
};

// X ファイルに書くマテリアル
struct XOutputMaterial {
    std::string sName;
    ufbx_vec4 diffuse{1.0, 1.0, 1.0, 1.0};
    std::string sTexture; // X ファイルからの相対パス（'/' 区切り）
};

// --- FBX のマテリアルから X ファイル用のマテリアルを作る処理 ---
XOutputMaterial MakeOutputMaterial(const ufbx_material* pMaterial, std::size_t nIndex, const std::filesystem::path& xFolder) {
    XOutputMaterial result;
    result.sName = XIdentifier(pMaterial ? std::string(pMaterial->name.data, pMaterial->name.length) : std::string(), nIndex);
    if (!pMaterial) {
        return result;
    }

    // 拡散色: FBX の Diffuse を優先し、無ければ PBR の Base Color（どの形式でも定義される）
    const ufbx_material_map& diffuse = pMaterial->fbx.diffuse_color.has_value ? pMaterial->fbx.diffuse_color : pMaterial->pbr.base_color;
    const ufbx_real dFactor = pMaterial->fbx.diffuse_factor.has_value ? pMaterial->fbx.diffuse_factor.value_real : 1.0;
    result.diffuse = {diffuse.value_vec3.x * dFactor, diffuse.value_vec3.y * dFactor, diffuse.value_vec3.z * dFactor, 1.0};
    if (pMaterial->pbr.opacity.has_value) {
        result.diffuse.w = pMaterial->pbr.opacity.value_real;
    }

    // テクスチャ: X ファイルからの相対パス（同じドライブで無い場合はファイル名だけ）
    const ufbx_texture* pTexture = diffuse.texture ? diffuse.texture : pMaterial->pbr.base_color.texture;
    if (pTexture && pTexture->filename.length > 0) {
        const auto texturePath = std::filesystem::u8path(std::string(pTexture->filename.data, pTexture->filename.length));
        std::error_code ec;
        auto relative = std::filesystem::relative(texturePath, xFolder, ec);
        if (ec || relative.empty()) {
            relative = texturePath.filename();
        }
        const auto text = relative.generic_u8string();
        result.sTexture.assign(reinterpret_cast<const char*>(text.data()), text.size());
    }
    return result;
}

// --- 有限の値を書く処理（NaN / 無限大は 0 にする） ---
void WriteNumber(std::ostringstream& out, double dValue) {
    out << (std::isfinite(dValue) ? dValue : 0.0);
}

// --- X ファイル（テキスト形式）の本文を作る処理 ---
std::string BuildXText(const std::vector<XVertex>& vVertices, const std::vector<std::uint32_t>& vFaceMaterials,
                       const std::vector<XOutputMaterial>& vMaterials, bool bHasUv) {
    std::ostringstream out;
    out.imbue(std::locale::classic()); // 地域設定に関係なく "." を小数点にする
    out << std::setprecision(7);

    const std::size_t nFaceCount = vVertices.size() / 3;
    out << "xof 0303txt 0032\n";
    out << "// Converted by DxStudio (FBX -> X)\n\n";

    // マテリアル（Mesh の MeshMaterialList から名前で参照する）
    for (const auto& material : vMaterials) {
        out << "Material " << material.sName << " {\n";
        out << "  ";
        WriteNumber(out, material.diffuse.x);
        out << ';';
        WriteNumber(out, material.diffuse.y);
        out << ';';
        WriteNumber(out, material.diffuse.z);
        out << ';';
        WriteNumber(out, material.diffuse.w);
        out << ";;\n";
        out << "  8.000000;\n";              // 鏡面反射の強さ
        out << "  0.000000;0.000000;0.000000;;\n"; // 鏡面反射色
        out << "  0.000000;0.000000;0.000000;;\n"; // 発光色
        if (!material.sTexture.empty()) {
            out << "  TextureFilename {\n    \"" << material.sTexture << "\";\n  }\n";
        }
        out << "}\n\n";
    }

    // 頂点（三角形の角ごと）
    out << "Mesh ConvertedMesh {\n";
    out << "  " << vVertices.size() << ";\n";
    for (std::size_t nIndex = 0; nIndex < vVertices.size(); ++nIndex) {
        const auto& position = vVertices[nIndex].position;
        out << "  ";
        WriteNumber(out, position.x);
        out << ';';
        WriteNumber(out, position.y);
        out << ';';
        WriteNumber(out, position.z);
        out << (nIndex + 1 < vVertices.size() ? ";,\n" : ";;\n");
    }

    // 面（3 頂点ずつ、番号は連番）
    out << "  " << nFaceCount << ";\n";
    for (std::size_t nFace = 0; nFace < nFaceCount; ++nFace) {
        out << "  3;" << nFace * 3 << ',' << nFace * 3 + 1 << ',' << nFace * 3 + 2;
        out << (nFace + 1 < nFaceCount ? ";,\n" : ";;\n");
    }

    // 法線
    out << "\n  MeshNormals {\n";
    out << "    " << vVertices.size() << ";\n";
    for (std::size_t nIndex = 0; nIndex < vVertices.size(); ++nIndex) {
        const auto& normal = vVertices[nIndex].normal;
        out << "    ";
        WriteNumber(out, normal.x);
        out << ';';
        WriteNumber(out, normal.y);
        out << ';';
        WriteNumber(out, normal.z);
        out << (nIndex + 1 < vVertices.size() ? ";,\n" : ";;\n");
    }
    out << "    " << nFaceCount << ";\n";
    for (std::size_t nFace = 0; nFace < nFaceCount; ++nFace) {
        out << "    3;" << nFace * 3 << ',' << nFace * 3 + 1 << ',' << nFace * 3 + 2;
        out << (nFace + 1 < nFaceCount ? ";,\n" : ";;\n");
    }
    out << "  }\n";

    // UV（X ファイルの V は下向きなので反転する）
    if (bHasUv) {
        out << "\n  MeshTextureCoords {\n";
        out << "    " << vVertices.size() << ";\n";
        for (std::size_t nIndex = 0; nIndex < vVertices.size(); ++nIndex) {
            const auto& uv = vVertices[nIndex].uv;
            out << "    ";
            WriteNumber(out, uv.x);
            out << ';';
            WriteNumber(out, 1.0 - uv.y);
            out << (nIndex + 1 < vVertices.size() ? ";,\n" : ";;\n");
        }
        out << "  }\n";
    }

    // 面ごとのマテリアル番号
    out << "\n  MeshMaterialList {\n";
    out << "    " << vMaterials.size() << ";\n";
    out << "    " << nFaceCount << ";\n";
    for (std::size_t nFace = 0; nFace < nFaceCount; ++nFace) {
        out << "    " << vFaceMaterials[nFace] << (nFace + 1 < nFaceCount ? ",\n" : ";\n");
    }
    for (const auto& material : vMaterials) {
        out << "    { " << material.sName << " }\n";
    }
    out << "  }\n";

    out << "}\n";
    return out.str();
}

} // namespace

//==================================================================================
// --- 重複しないファイル名の作成処理 ---
//==================================================================================
std::filesystem::path UniqueFilePath(const std::filesystem::path& folder, const std::wstring& sStem,
                                     const std::wstring& sExtension) {
    std::error_code ec;
    auto candidate = folder / (sStem + sExtension);
    for (int nNumber = 1; std::filesystem::exists(candidate, ec) && nNumber < 10000; ++nNumber) {
        candidate = folder / (sStem + L"_" + std::to_wstring(nNumber) + sExtension);
    }
    return candidate;
}

//==================================================================================
// --- X ファイルの Material の読み取り処理 ---
//==================================================================================
bool ReadXMaterials(const std::filesystem::path& xFile, std::vector<XMaterialInfo>& vOut, std::string& error) {
    error.clear();
    vOut.clear();

    std::string sText;
    if (!ReadWholeFile(xFile, sText, error)) {
        return false;
    }

    // ヘッダー: "xof 0303txt 0032"（txt 以外のバイナリ / 圧縮形式は読めない）
    if (sText.size() < 16 || sText.compare(0, 4, "xof ") != 0) {
        error = "This is not a DirectX X file: " + PathUtf8(xFile);
        return false;
    }
    if (sText.compare(8, 3, "txt") != 0) {
        error = "Only text X files are supported (this file is binary or compressed).";
        return false;
    }

    constexpr std::string_view WORD = "Material";
    std::size_t nPos = sText.find(WORD);
    while (nPos != std::string::npos) {
        std::size_t nNext = nPos + WORD.size();

        // "MeshMaterialList" などの一部や、テンプレートの宣言は読み飛ばす
        const bool bWordStart = nPos == 0 || !IsIdentifierChar(sText[nPos - 1]);
        const bool bWordEnd = nNext >= sText.size() || !IsIdentifierChar(sText[nNext]);
        if (bWordStart && bWordEnd && !IsTemplateDeclaration(sText, nPos)) {
            XMaterialInfo material;
            if (ParseMaterialAt(sText, nPos, material, nNext)) {
                vOut.push_back(std::move(material));
            }
        }

        nPos = sText.find(WORD, nNext);
    }
    return true;
}

//==================================================================================
// --- X ファイルからのマテリアルファイルの作成処理 ---
//==================================================================================
bool CreateMaterialsFromX(const std::filesystem::path& projectRoot, const std::filesystem::path& xFile,
                          std::vector<std::string>& vCreatedAssets, std::string& error) {
    vCreatedAssets.clear();

    std::vector<XMaterialInfo> vMaterials;
    if (!ReadXMaterials(xFile, vMaterials, error)) {
        return false;
    }

    // Material が無い X ファイルでも、白のマテリアルを 1 つ作る
    if (vMaterials.empty()) {
        vMaterials.push_back({});
    }

    const auto folder = xFile.parent_path();
    const std::wstring sStem = xFile.stem().wstring();
    std::error_code ec;
    const auto root = std::filesystem::weakly_canonical(projectRoot, ec);

    for (std::size_t nIndex = 0; nIndex < vMaterials.size(); ++nIndex) {
        const auto& source = vMaterials[nIndex];

        // ファイル名: 1 つなら <X の名前>、複数なら <X の名前>_<マテリアル名 または 番号>
        std::wstring sFileStem = sStem;
        if (vMaterials.size() > 1) {
            sFileStem += L"_" + (source.sName.empty() ? std::to_wstring(nIndex) : SafeFileStem(source.sName));
        }
        const auto output = UniqueFilePath(folder, sFileStem, L".dxmat");

        // Direct3D 11 / 12 の描画は baseColor を使う（拡散色をそのまま入れる）
        Material material;
        material.sName = source.sName.empty() ? PathUtf8(xFile.stem()) : source.sName;
        material.baseColor = source.diffuse;
        material.sShaderAsset = "Default.hlsl";

        // テクスチャ: X ファイルからの相対パスを、プロジェクトからの相対パスにする
        if (!source.sTexture.empty()) {
            const auto texture = std::filesystem::weakly_canonical(folder / std::filesystem::u8path(source.sTexture), ec);
            auto relative = texture.lexically_relative(root);
            if (relative.empty() || *relative.begin() == L"..") {
                relative = std::filesystem::u8path(source.sTexture); // プロジェクトの外: 書いてあるまま残す
            }
            material.sTextureAsset = PathUtf8(relative.generic_wstring());
        }

        if (!SaveMaterial(output, material, error)) {
            return false;
        }

        // 作ったファイルをプロジェクトからの相対パスで返す
        auto asset = std::filesystem::weakly_canonical(output, ec).lexically_relative(root);
        if (asset.empty() || *asset.begin() == L"..") {
            asset = output;
        }
        vCreatedAssets.push_back(PathUtf8(asset.generic_wstring()));
    }
    return true;
}

//==================================================================================
// --- FBX → X ファイルの変換処理 ---
//==================================================================================
bool ConvertFbxToX(const std::filesystem::path& fbxFile, const std::filesystem::path& xFile, std::string& error) {
    error.clear();

    // FBX の読み込み（法線が無ければ生成する）
    ufbx_load_opts opts{};
    opts.generate_missing_normals = true;
    ufbx_error loadError{};
    ufbx_scene* pScene = ufbx_load_file(PathUtf8(fbxFile).c_str(), &opts, &loadError);
    if (!pScene) {
        error = "Cannot read the FBX file: " + std::string(loadError.description.data, loadError.description.length);
        return false;
    }

    std::vector<XVertex> vVertices;
    std::vector<std::uint32_t> vFaceMaterials;
    std::vector<XOutputMaterial> vMaterials;
    std::map<const ufbx_material*, std::uint32_t> materialIndex; // FBX のマテリアル → X のマテリアル番号
    bool bHasUv = false;
    const auto xFolder = xFile.parent_path();

    // マテリアルの番号を取得する（初めてのマテリアルなら追加する）
    auto findMaterial = [&](const ufbx_material* pMaterial) {
        const auto found = materialIndex.find(pMaterial);
        if (found != materialIndex.end()) {
            return found->second;
        }

        const auto nIndex = static_cast<std::uint32_t>(vMaterials.size());
        vMaterials.push_back(MakeOutputMaterial(pMaterial, nIndex, xFolder));
        materialIndex.emplace(pMaterial, nIndex);
        return nIndex;
    };

    // 全メッシュ・全インスタンスの面を三角形にし、ワールド座標で集める
    // （エンジンが FBX を描くときと同じ変換）
    for (std::size_t nMesh = 0; nMesh < pScene->meshes.count; ++nMesh) {
        const ufbx_mesh* pMesh = pScene->meshes.data[nMesh];
        if (!pMesh || !pMesh->vertex_position.exists) {
            continue;
        }

        bHasUv = bHasUv || pMesh->vertex_uv.exists;
        std::vector<std::uint32_t> vIndices(std::max<std::size_t>(3, pMesh->max_face_triangles * 3));
        const std::size_t nInstanceCount = std::max<std::size_t>(1, pMesh->instances.count);

        for (std::size_t nInstance = 0; nInstance < nInstanceCount; ++nInstance) {
            const ufbx_matrix* pMatrix = pMesh->instances.count ? &pMesh->instances.data[nInstance]->geometry_to_world : nullptr;
            const ufbx_matrix normalMatrix = pMatrix ? ufbx_matrix_for_normals(pMatrix) : ufbx_matrix{};

            for (std::size_t nFace = 0; nFace < pMesh->faces.count; ++nFace) {
                // 面のマテリアル
                const ufbx_material* pMaterial = nullptr;
                if (pMesh->face_material.count > nFace && pMesh->face_material.data[nFace] < pMesh->materials.count) {
                    pMaterial = pMesh->materials.data[pMesh->face_material.data[nFace]];
                }
                const std::uint32_t nMaterial = findMaterial(pMaterial);

                // 多角形を三角形に分け、角ごとに位置・法線・UV を取り出す
                const std::uint32_t nTriangles = ufbx_triangulate_face(vIndices.data(), vIndices.size(), pMesh, pMesh->faces.data[nFace]);
                for (std::size_t nCorner = 0; nCorner < static_cast<std::size_t>(nTriangles) * 3; ++nCorner) {
                    const std::uint32_t nVertex = vIndices[nCorner];
                    XVertex vertex;
                    vertex.position = ufbx_get_vertex_vec3(&pMesh->skinned_position, nVertex);
                    if (pMesh->vertex_normal.exists) {
                        vertex.normal = ufbx_get_vertex_vec3(&pMesh->vertex_normal, nVertex);
                    }
                    if (pMesh->vertex_uv.exists) {
                        vertex.uv = ufbx_get_vertex_vec2(&pMesh->vertex_uv, nVertex);
                    }

                    if (pMatrix) {
                        vertex.position = ufbx_transform_position(pMatrix, vertex.position);
                        vertex.normal = ufbx_vec3_normalize(ufbx_transform_direction(&normalMatrix, vertex.normal));
                    }
                    vVertices.push_back(vertex);
                }

                for (std::uint32_t nTriangle = 0; nTriangle < nTriangles; ++nTriangle) {
                    vFaceMaterials.push_back(nMaterial);
                }
            }
        }
    }
    ufbx_free_scene(pScene);

    // 書き出せる大きさか
    if (vVertices.empty()) {
        error = "The FBX file contains no mesh to convert.";
        return false;
    }
    if (vVertices.size() > MAX_VERTICES) {
        error = "The mesh is too large to convert (" + std::to_string(vVertices.size()) + " vertices, maximum " +
                std::to_string(MAX_VERTICES) + ").";
        return false;
    }

    // マテリアルが 1 つも無ければ白を 1 つ用意する
    if (vMaterials.empty()) {
        findMaterial(nullptr);
    }

    return WriteFileAtomically(xFile, BuildXText(vVertices, vFaceMaterials, vMaterials, bHasUv), error);
}

} // namespace dxs
