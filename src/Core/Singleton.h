#pragma once

// Singleton interface (from cpp_TKG singleton.h). Derive as
//   class CFoo : public ISingleton<CFoo> { friend ISingleton<CFoo>; ... };
// and access the single instance with CFoo::GetInstance().

namespace dxs {

template<class T> class ISingleton {
public:
    static T* GetInstance() {
        static T instance;
        return &instance;
    }

protected:
    ISingleton() = default;
    ~ISingleton() = default;
};

} // namespace dxs
