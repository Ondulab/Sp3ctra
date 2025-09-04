/*
 * audio_diagnostic.c
 * Diagnostic tool to identify audio output issues
 */

#include <stdio.h>
#include <math.h>
#include <string.h>
#include "../config/config.h"
#include "../synthesis/additive/synth_additive.h"
#include "../audio/buffers/audio_image_buffers.h"

// External references
extern AudioDataBuffers buffers_L[2];
extern AudioDataBuffers buffers_R[2];
extern volatile int current_buffer_index;
extern AudioImageBuffers g_audio_image_buffers;

void diagnose_audio_pipeline(void) {
    printf("\n========== AUDIO PIPELINE DIAGNOSTIC ==========\n");
    
    // 1. Check buffer indices
    int current_idx = current_buffer_index;
    printf("📍 Current buffer index: %d\n", current_idx);
    
    // 2. Check additive synthesis buffers
    printf("\n🎵 ADDITIVE SYNTHESIS BUFFERS:\n");
    
    // Check Left buffer
    float left_min = 0.0f, left_max = 0.0f, left_sum = 0.0f;
    int left_ready = buffers_L[current_idx].ready;
    printf("  Left Buffer[%d]: ready=%d\n", current_idx, left_ready);
    
    if (left_ready) {
        for (int i = 0; i < AUDIO_BUFFER_SIZE; i++) {
            float val = buffers_L[current_idx].data[i];
            if (i == 0 || val < left_min) left_min = val;
            if (i == 0 || val > left_max) left_max = val;
            left_sum += val * val;
        }
        float left_rms = sqrtf(left_sum / AUDIO_BUFFER_SIZE);
        printf("    Min: %.6f, Max: %.6f, RMS: %.6f\n", left_min, left_max, left_rms);
        
        // Show first 5 samples
        printf("    First 5 samples: ");
        for (int i = 0; i < 5 && i < AUDIO_BUFFER_SIZE; i++) {
            printf("%.6f ", buffers_L[current_idx].data[i]);
        }
        printf("\n");
    }
    
    // Check Right buffer
    float right_min = 0.0f, right_max = 0.0f, right_sum = 0.0f;
    int right_ready = buffers_R[current_idx].ready;
    printf("  Right Buffer[%d]: ready=%d\n", current_idx, right_ready);
    
    if (right_ready) {
        for (int i = 0; i < AUDIO_BUFFER_SIZE; i++) {
            float val = buffers_R[current_idx].data[i];
            if (i == 0 || val < right_min) right_min = val;
            if (i == 0 || val > right_max) right_max = val;
            right_sum += val * val;
        }
        float right_rms = sqrtf(right_sum / AUDIO_BUFFER_SIZE);
        printf("    Min: %.6f, Max: %.6f, RMS: %.6f\n", right_min, right_max, right_rms);
        
        // Show first 5 samples
        printf("    First 5 samples: ");
        for (int i = 0; i < 5 && i < AUDIO_BUFFER_SIZE; i++) {
            printf("%.6f ", buffers_R[current_idx].data[i]);
        }
        printf("\n");
    }
    
    // 3. Check audio image buffers
    printf("\n📸 AUDIO IMAGE BUFFERS:\n");
    printf("  Active buffer: %d\n", g_audio_image_buffers.active_buffer);
    
    // Check grayscale data
    int32_t gray_min = 65535, gray_max = 0;
    int32_t gray_sum = 0;
    int non_zero_count = 0;
    
    for (int i = 0; i < CIS_MAX_PIXELS_NB; i++) {
        int32_t val = g_audio_image_buffers.grayscale_data[g_audio_image_buffers.active_buffer][i];
        if (val < gray_min) gray_min = val;
        if (val > gray_max) gray_max = val;
        gray_sum += val;
        if (val > 0) non_zero_count++;
    }
    
    float gray_avg = (float)gray_sum / CIS_MAX_PIXELS_NB;
    printf("  Grayscale data: Min=%d, Max=%d, Avg=%.2f\n", gray_min, gray_max, gray_avg);
    printf("  Non-zero pixels: %d/%d (%.1f%%)\n", 
           non_zero_count, CIS_MAX_PIXELS_NB, 
           (float)non_zero_count * 100.0f / CIS_MAX_PIXELS_NB);
    
    // 4. Check wave oscillator states
    printf("\n🌊 WAVE OSCILLATOR STATES:\n");
    int active_oscillators = 0;
    float total_volume = 0.0f;
    
    for (int i = 0; i < NUMBER_OF_NOTES && i < 10; i++) { // Check first 10
        if (waves[i].current_volume > 0.001f) {
            active_oscillators++;
            total_volume += waves[i].current_volume;
            if (i < 5) { // Show details for first 5 active
                printf("  Wave[%d]: volume=%.4f, target=%.4f, freq=%.2fHz\n",
                       i, waves[i].current_volume, waves[i].target_volume,
                       waves[i].frequency);
            }
        }
    }
    printf("  Active oscillators: %d/%d\n", active_oscillators, NUMBER_OF_NOTES);
    printf("  Total volume sum: %.4f\n", total_volume);
    
    // 5. Check normalization factors
    printf("\n🔧 NORMALIZATION FACTORS:\n");
    printf("  VOLUME_AMP_RESOLUTION: %d\n", VOLUME_AMP_RESOLUTION);
    printf("  WAVE_AMP_RESOLUTION: %d\n", WAVE_AMP_RESOLUTION);
    printf("  MASTER_VOLUME: %.4f\n", MASTER_VOLUME);
    
    // 6. Diagnosis summary
    printf("\n📊 DIAGNOSIS SUMMARY:\n");
    
    if (left_rms < 0.0001f && right_rms < 0.0001f) {
        printf("  ❌ NO AUDIO OUTPUT DETECTED\n");
        
        if (gray_avg < 100) {
            printf("  ⚠️  Image data is very dark (avg=%.2f)\n", gray_avg);
        }
        
        if (active_oscillators == 0) {
            printf("  ⚠️  No active oscillators\n");
        }
        
        if (!left_ready || !right_ready) {
            printf("  ⚠️  Audio buffers not ready\n");
        }
        
        if (total_volume < 0.001f) {
            printf("  ⚠️  Total oscillator volume is near zero\n");
        }
    } else {
        printf("  ✅ Audio output detected\n");
        printf("  L channel RMS: %.6f\n", left_rms);
        printf("  R channel RMS: %.6f\n", right_rms);
        
        // Check for mono/stereo
        float lr_diff = fabsf(left_rms - right_rms);
        if (lr_diff < 0.0001f) {
            printf("  📢 Output appears to be MONO (L=R)\n");
        } else {
            printf("  🎧 Output appears to be STEREO (L≠R)\n");
        }
    }
    
    printf("================================================\n\n");
}

// Function to be called periodically from main audio thread
void audio_diagnostic_periodic(void) {
    static int call_counter = 0;
    
    // Run diagnostic every 5 seconds (assuming ~86 calls/sec at 44.1kHz)
    if (++call_counter >= 430) {
        call_counter = 0;
        diagnose_audio_pipeline();
    }
}
