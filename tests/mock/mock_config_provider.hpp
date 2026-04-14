// tests/mock/mock_config_provider.hpp
#pragma once

#include <gmock/gmock.h>
#include "interfaces.hpp"

namespace broker {

class MockConfigProvider : public IConfigProvider {
public:
    MOCK_METHOD(const Config&, GetConfig, (), (const, override));
    
    void SetConfig(const Config& config) {
        config_ = config;
        ON_CALL(*this, GetConfig()).WillByDefault(::testing::ReturnRef(config_));
    }
    
private:
    Config config_;
};

} // namespace broker