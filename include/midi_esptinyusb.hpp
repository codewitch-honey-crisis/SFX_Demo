#pragma once
#include <sfx_midi_core.hpp>
namespace arduino {
    class midi_esptinyusb final : public sfx::midi_output {
    public:
        sfx::sfx_result initialize(const char* device_name = nullptr);
        inline bool initialized() const;
        virtual sfx::sfx_result send(const sfx::midi_message& message);
    };
}