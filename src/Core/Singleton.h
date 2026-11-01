//==================================================================================
//
// シングルトンインターフェースのヘッダーファイル [Singleton.h]
//
// cpp_TKG の singleton.h と同じ形。次のように継承して使う。
//   class CFoo : public ISingleton<CFoo> { friend ISingleton<CFoo>; ... };
// 唯一のインスタンスは CFoo::GetInstance() で取得する。
//
//==================================================================================
#pragma once

namespace dxs {

//**********************************************************************************
// *** シングルトンインターフェース ***
//**********************************************************************************
template<class T> class ISingleton {
public:
    // --- インスタンス取得処理（初回呼び出し時に生成される） ---
    static T* GetInstance() {
        static T instance;
        return &instance;
    }

protected:
    ISingleton() = default;  // 外部からの生成を禁止
    ~ISingleton() = default; // 外部からの破棄を禁止
};

} // namespace dxs
