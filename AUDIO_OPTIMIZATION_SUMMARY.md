# Audio Optimization Summary for Raspberry Pi

## 🎯 Problem Identified
Le son était haché sur Raspberry Pi alors qu'il fonctionnait correctement sur Mac, probablement dû à:
- Puissance de calcul limitée du Pi
- Buffers audio trop petits (512 frames = ~5.33ms)
- Processing audio complexe (réverbération ZitaRev1, égaliseur 3 bandes, 2 synthétiseurs)

## 🔧 Optimizations Applied

### Phase 1: Buffer Size Optimization (COMPLETED)

**File: `src/core/config.h`**
```cpp
// Before
#define AUDIO_BUFFER_SIZE (512)

// After  
#define AUDIO_BUFFER_SIZE (1024) // Increased from 512 to reduce underruns on Pi
```

**File: `src/core/audio_rtaudio.cpp`**
```cpp
// Before
options.numberOfBuffers = 4;

// After
options.numberOfBuffers = 8; // Increased from 4 to 8 for better stability on Pi
```

### Impact of Changes
- **Latency**: ~5.33ms → ~10.67ms (doubled for stability)
- **Buffer memory**: 2x plus de mémoire tampon
- **Stability**: Significantly reduced risk of audio underruns

## 🧪 Testing Tools Created

### 1. `test_audio_optimization_pi.sh`
- Tests all available audio devices (1, 2, 5)
- Monitors for ALSA errors and buffer underruns
- Provides system performance analysis
- Gives specific recommendations

### 2. `deploy_audio_fix_to_pi.sh`
- Automatic deployment to Pi via SSH/SCP
- Tests connection before deployment
- Can run tests immediately after deployment

## 🚀 Usage Instructions

### Option 1: Deploy and Test Automatically
```bash
./deploy_audio_fix_to_pi.sh sp3ctra@pi
```

### Option 2: Manual Deployment
```bash
# Copy files to Pi
scp src/core/config.h sp3ctra@pi:~/Sp3ctra_Application/src/core/
scp src/core/audio_rtaudio.cpp sp3ctra@pi:~/Sp3ctra_Application/src/core/
scp test_audio_optimization_pi.sh sp3ctra@pi:~/Sp3ctra_Application/

# SSH to Pi and test
ssh sp3ctra@pi
cd ~/Sp3ctra_Application
chmod +x test_audio_optimization_pi.sh
./test_audio_optimization_pi.sh
```

## 📊 Expected Results

### Before Optimization
```
RtAudio initialisé: SR=96000Hz, BS=512 frames, Latence=5.33333ms
```

### After Optimization
```
RtAudio initialisé: SR=96000Hz, BS=1024 frames, Latence=10.6667ms
```

## 🔄 Next Steps if Still Choppy

### Phase 2: Further Buffer Increase
If audio is still choppy, increase buffer size to 2048:
```cpp
#define AUDIO_BUFFER_SIZE (2048) // ~21.33ms latency
```

### Phase 3: Processing Optimization
- Temporarily disable reverb: Add `--no-reverb` flag support
- Temporarily disable EQ: Comment out equalizer processing
- Test with simpler waveforms

### Phase 4: System Optimization
- Set CPU governor to "performance"
- Stop unnecessary services
- Increase Pi's GPU memory split
- Use faster SD card (Class 10+)

### Phase 5: Alternative Configurations
- Test with 48kHz instead of 96kHz
- Use different audio devices (USB vs built-in)
- Consider external USB sound card

## 🎵 Audio Device Testing Priority

Based on your logs:
1. **Device 5**: Steinberg UR22C (96kHz capable)
2. **Device 1**: USB SPDIF Adapter,0 
3. **Device 2**: USB SPDIF Adapter,1

## ⚡ Performance Monitoring

The test script will check:
- ✅ System load and CPU usage
- ✅ ALSA error count
- ✅ Buffer underrun detection
- ✅ Audio initialization success
- ✅ CPU governor and frequency

## 🎯 Success Criteria

The optimization is successful if:
- ✅ Audio initialization succeeds at 96kHz
- ✅ No buffer underruns during playback
- ✅ Smooth, continuous audio output
- ✅ MIDI controller responds properly
- ✅ System load remains manageable

---

**Created**: 2025-01-06
**Status**: Phase 1 Complete - Ready for Testing
**Next Action**: Deploy to Pi and run tests
