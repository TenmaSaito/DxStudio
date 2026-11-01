//==================================================================================
//
// アセットのサムネイルのテスト [ThumbnailTests.cpp]
//
// 組み込みの基本図形（.x）とマテリアル（.dxmat）のサムネイルを描き、形と色を確認する。
// 環境変数 DXS_THUMBNAIL_DUMP にフォルダーを指定すると、描いた画像を BMP で書き出す。
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "AssetThumbnail.h"
#include "Engine.h"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>

namespace {

namespace fs = std::filesystem;
constexpr int THUMB_SIZE = 64; // サムネイルの大きさ

// --- 画素の取り出し（A / R / G / B） ---
std::uint32_t Alpha(std::uint32_t uPixel) {
    return uPixel >> 24;
}

std::uint32_t Red(std::uint32_t uPixel) {
    return (uPixel >> 16) & 0xFF;
}

std::uint32_t Blue(std::uint32_t uPixel) {
    return uPixel & 0xFF;
}

//==================================================================================
// --- BMP の書き出し処理（確認用。32bit、上の行から） ---
//==================================================================================
void DumpBitmap(const fs::path& file, const std::vector<std::uint32_t>& vPixels, int nSize) {
    const char* pFolder = std::getenv("DXS_THUMBNAIL_DUMP");
    if (!pFolder) return;

    const std::uint32_t uImageBytes = static_cast<std::uint32_t>(vPixels.size() * 4);
    BITMAPFILEHEADER fileHeader{0x4D42, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + uImageBytes, 0, 0,
                                sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)};
    BITMAPINFOHEADER infoHeader{sizeof(BITMAPINFOHEADER), nSize, -nSize, 1, 32, BI_RGB, uImageBytes};
    std::ofstream out(fs::path(pFolder) / file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
    out.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader));

    // 背景（パネルの色）に重ねて書く（見やすくするため）
    for (const std::uint32_t uPixel : vPixels) {
        const std::uint32_t uA = Alpha(uPixel);
        auto mix = [uA](std::uint32_t uValue, std::uint32_t uBack) {
            return (uValue * uA + uBack * (255 - uA)) / 255;
        };
        const std::uint32_t uOut = (mix(Red(uPixel), 56) << 16) | (mix((uPixel >> 8) & 0xFF, 56) << 8) | mix(Blue(uPixel), 56);
        out.write(reinterpret_cast<const char*>(&uOut), 4);
    }
}

} // namespace

//==================================================================================
// --- メイン関数 ---
//==================================================================================
int main() {
    const fs::path root = fs::temp_directory_path() / L"DxStudioThumbnailTests";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    std::string error;
    assert(dxs::EnsurePrimitiveAssets(root, error));

    // 1. 立方体: 中央は描かれ、四隅は透明。斜め上から見るので 3 面の明るさが違う。
    std::vector<std::uint32_t> vPixels;
    assert(dxs::RenderAssetThumbnail(root / L"Assets" / L"Primitives" / L"Cube.x", THUMB_SIZE, vPixels));
    assert(vPixels.size() == static_cast<size_t>(THUMB_SIZE * THUMB_SIZE));
    assert(Alpha(vPixels[(THUMB_SIZE / 2) * THUMB_SIZE + THUMB_SIZE / 2]) == 255);
    assert(Alpha(vPixels[0]) == 0 && Alpha(vPixels[THUMB_SIZE - 1]) == 0 && Alpha(vPixels[THUMB_SIZE * THUMB_SIZE - 1]) == 0);
    std::vector<std::uint32_t> vShades;
    for (const std::uint32_t uPixel : vPixels) {
        if (Alpha(uPixel) == 255 && std::find(vShades.begin(), vShades.end(), Red(uPixel)) == vShades.end()) {
            vShades.push_back(Red(uPixel));
        }
    }

    assert(vShades.size() >= 3);
    DumpBitmap(L"Cube.bmp", vPixels, THUMB_SIZE);

    // 2. その他の基本図形もすべて描ける（2D の図形は正面から）
    for (const auto& info : dxs::PRIMITIVES) {
        const fs::path file = root / fs::u8path(dxs::PrimitiveAssetPath(info.shape));
        assert(dxs::RenderAssetThumbnail(file, THUMB_SIZE, vPixels));
        assert(Alpha(vPixels[(THUMB_SIZE / 2) * THUMB_SIZE + THUMB_SIZE / 2]) == 255);
        DumpBitmap(fs::u8path(std::string(info.pName) + ".bmp"), vPixels, THUMB_SIZE);
    }

    // 3. マテリアル: Base Color の球（赤なら赤が強く、青は弱い）
    dxs::Material material;
    material.baseColor = {0.9f, 0.1f, 0.1f, 1.0f};
    assert(dxs::SaveMaterial(root / L"Red.dxmat", material, error));
    assert(dxs::RenderAssetThumbnail(root / L"Red.dxmat", THUMB_SIZE, vPixels));
    const std::uint32_t uCenter = vPixels[(THUMB_SIZE / 2) * THUMB_SIZE + THUMB_SIZE / 2];
    assert(Alpha(uCenter) == 255 && Red(uCenter) > Blue(uCenter) + 80);
    assert(Alpha(vPixels[0]) == 0);
    DumpBitmap(L"Red.bmp", vPixels, THUMB_SIZE);

    // 4. 対象外 / 読めないファイルは false
    assert(!dxs::HasAssetThumbnail(L"Readme.txt"));
    assert(!dxs::RenderAssetThumbnail(root / L"Readme.txt", THUMB_SIZE, vPixels));
    assert(!dxs::RenderAssetThumbnail(root / L"Missing.x", THUMB_SIZE, vPixels));

    fs::remove_all(root, ec);
    std::cout << "Asset thumbnail (model shape, primitives, material sphere) tests passed.\n";
    return 0;
}
