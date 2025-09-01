#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <portaudio.h>

typedef struct {
    int16_t *samples;
    long totalFrames;
    long currentFrame;
    int channels;
    int sampleRate;
} AudioData;

int loadWAV(const char* filename, AudioData* audioData) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        printf("❌ Không thể mở file: %s\n", filename);
        return -1;
    }

    char header[44];
    fread(header, 1, 44, file);
    
    if (strncmp(header, "RIFF", 4) != 0 || strncmp(header + 8, "WAVE", 4) != 0) {
        printf("❌ File không phải định dạng WAV hợp lệ\n");
        fclose(file);
        return -1;
    }
    
    audioData->channels = *(int16_t*)(header + 22);
    audioData->sampleRate = *(int32_t*)(header + 24);
    int bitsPerSample = *(int16_t*)(header + 34);
    int dataSize = *(int32_t*)(header + 40);
    
    printf("📊 Thông tin file WAV:\n");
    printf("   🎵 Channels: %d\n", audioData->channels);
    printf("   🔊 Sample Rate: %d Hz\n", audioData->sampleRate);
    printf("   🎚️  Bits per Sample: %d\n", bitsPerSample);
    printf("   📏 Data Size: %d bytes\n", dataSize);
    
    if (bitsPerSample != 16) {
        printf("❌ Chỉ hỗ trợ file WAV 16-bit. File này là %d-bit\n", bitsPerSample);
        fclose(file);
        return -1;
    }
    
    audioData->totalFrames = dataSize / (audioData->channels * 2); // 2 bytes per sample
    audioData->currentFrame = 0;
    
    audioData->samples = (int16_t*)malloc(dataSize);
    if (!audioData->samples) {
        printf("❌ Không đủ memory để load file\n");
        fclose(file);
        return -1;
    }
    
    fread(audioData->samples, 1, dataSize, file);
    fclose(file);
    
    printf("✅ Load file WAV thành công: %ld frames\n", audioData->totalFrames);
    return 0;
}

// Callback function cho PortAudio
static int audioCallback(const void *inputBuffer, void *outputBuffer,
                        unsigned long framesPerBuffer,
                        const PaStreamCallbackTimeInfo* timeInfo,
                        PaStreamCallbackFlags statusFlags,
                        void *userData) {
    
    AudioData *audioData = (AudioData*)userData;
    int16_t *output = (int16_t*)outputBuffer;
    
    long framesRemaining = audioData->totalFrames - audioData->currentFrame;
    long framesToCopy = (framesRemaining < framesPerBuffer) ? framesRemaining : framesPerBuffer;
    
    if (framesToCopy > 0) {
        memcpy(output, 
               &audioData->samples[audioData->currentFrame * audioData->channels], 
               framesToCopy * audioData->channels * sizeof(int16_t));
        
        audioData->currentFrame += framesToCopy;
        
        if (framesToCopy < framesPerBuffer) {
            memset(&output[framesToCopy * audioData->channels], 0,
                   (framesPerBuffer - framesToCopy) * audioData->channels * sizeof(int16_t));
        }
    } else {
        memset(output, 0, framesPerBuffer * audioData->channels * sizeof(int16_t));
        return paComplete;
    }
    
    return paContinue;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("📖 Cách sử dụng: %s <file.wav>\n", argv[0]);
        printf("   Ví dụ: %s music.wav\n", argv[0]);
        return 1;
    }
    
    printf("🎵 PortAudio WAV Player\n");
    printf("=======================\n");
    
    // Khởi tạo PortAudio
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        printf("❌ Lỗi khởi tạo PortAudio: %s\n", Pa_GetErrorText(err));
        return 1;
    }
    
    // Load file WAV
    AudioData audioData;
    if (loadWAV(argv[1], &audioData) != 0) {
        Pa_Terminate();
        return 1;
    }
    
    // Cấu hình stream parameters
    PaStreamParameters outputParameters;
    outputParameters.device = Pa_GetDefaultOutputDevice();
    if (outputParameters.device == paNoDevice) {
        printf("❌ Không tìm thấy output device\n");
        free(audioData.samples);
        Pa_Terminate();
        return 1;
    }
    
    outputParameters.channelCount = audioData.channels;
    outputParameters.sampleFormat = paInt16;
    outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;
    
    // Mở stream
    PaStream *stream;
    err = Pa_OpenStream(&stream,
                        NULL, // no input
                        &outputParameters,
                        audioData.sampleRate,
                        256, // frames per buffer
                        paClipOff,
                        audioCallback,
                        &audioData);
    
    if (err != paNoError) {
        printf("❌ Lỗi mở stream: %s\n", Pa_GetErrorText(err));
        free(audioData.samples);
        Pa_Terminate();
        return 1;
    }
    
    err = Pa_StartStream(stream);
    if (err != paNoError) {
        printf("❌ Lỗi start stream: %s\n", Pa_GetErrorText(err));
        Pa_CloseStream(stream);
        free(audioData.samples);
        Pa_Terminate();
        return 1;
    }
    
    printf("\n🎶 Đang phát nhạc... Nhấn Enter để dừng\n");
    
    while (Pa_IsStreamActive(stream)) {
        Pa_Sleep(100); // Sleep 100ms
        
        // Kiểm tra xem có input từ keyboard không (non-blocking check)
        // Đây là cách đơn giản, trong thực tế có thể dùng cách phức tạp hơn
    }
    
    printf("⏹️  Dừng phát nhạc...\n");
    
    // Dọn dẹp
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    free(audioData.samples);
    Pa_Terminate();
    
    printf("✅ Hoàn tất!\n");
    return 0;
}