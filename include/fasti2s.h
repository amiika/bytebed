#pragma once

#include <driver/i2s_std.h>

class FastI2S {
private:
    i2s_chan_handle_t tx_chan = nullptr;
    
public:
    void begin(uint32_t sample_rate) {
        if (tx_chan != nullptr) {
            i2s_channel_disable(tx_chan);
            i2s_del_channel(tx_chan);
            tx_chan = nullptr;
        }

        i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
        chan_cfg.dma_desc_num = 6;
        chan_cfg.dma_frame_num = 512;
        chan_cfg.auto_clear = true;      
        
        if (i2s_new_channel(&chan_cfg, &tx_chan, NULL) != ESP_OK) return;

        i2s_std_config_t std_cfg = {
            .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
            .gpio_cfg = {
                .mclk = I2S_GPIO_UNUSED, 
                .bclk = (gpio_num_t)41,
                .ws   = (gpio_num_t)43,
                .dout = (gpio_num_t)42,
                .din  = I2S_GPIO_UNUSED,
                .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
            },
        };
        
        i2s_channel_init_std_mode(tx_chan, &std_cfg);
        i2s_channel_enable(tx_chan);
    }

    void setRate(uint32_t sample_rate) {
        if (!tx_chan) return;
        i2s_channel_disable(tx_chan);
        i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
        i2s_channel_reconfig_std_clock(tx_chan, &clk_cfg);
        i2s_channel_enable(tx_chan);
    }

    // By making this inline in a header, we eliminate function-call overhead in the DSP loop
    inline void pushStereoBlock(int16_t* buffer, size_t frames) {
        if (!tx_chan) return;
        size_t bytes_written;
        // Native silicon blocking. Core 1 sleeps instantly until the exact microsecond the DMA needs data.
        i2s_channel_write(tx_chan, buffer, frames * 4, &bytes_written, portMAX_DELAY);
    }
};