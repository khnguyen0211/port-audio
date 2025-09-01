#include <iostream>
#include <fstream>
#include <vector>
#include <memory>
#include <string>
#include <cstring>  // for memcpy, memset, strncmp
#include <cstdint>  // for int16_t, int32_t
#include <iomanip>  // for setprecision
#include <portaudio.h>

class WAVPlayer {
private:
    struct AudioData {
        std::vector<int16_t> samples;
        size_t totalFrames;
        size_t currentFrame;
        int channels;
        unsigned int sampleRate;  // Đổi thành unsigned để tránh warning
        
        AudioData() : totalFrames(0), currentFrame(0), channels(0), sampleRate(0) {}
    };
    
    std::unique_ptr<AudioData> audioData;
    PaStream* stream;
    bool isInitialized;
    
    // Static callback wrapper
    static int audioCallbackWrapper(const void *inputBuffer, void *outputBuffer,
                                  unsigned long framesPerBuffer,
                                  const PaStreamCallbackTimeInfo* timeInfo,
                                  PaStreamCallbackFlags statusFlags,
                                  void *userData) {
        return static_cast<WAVPlayer*>(userData)->audioCallback(
            inputBuffer, outputBuffer, framesPerBuffer, timeInfo, statusFlags);
    }
    
    // Instance callback method
    int audioCallback(const void *inputBuffer, void *outputBuffer,
                     unsigned long framesPerBuffer,
                     const PaStreamCallbackTimeInfo* timeInfo,
                     PaStreamCallbackFlags statusFlags) {
        
        int16_t* output = static_cast<int16_t*>(outputBuffer);
        
        // Tính số frame còn lại
        size_t framesRemaining = audioData->totalFrames - audioData->currentFrame;
        size_t framesToCopy = std::min(static_cast<size_t>(framesPerBuffer), framesRemaining);
        
        if (framesToCopy > 0) {
            // Copy dữ liệu âm thanh
            size_t samplesToCopy = framesToCopy * audioData->channels;
            std::memcpy(output, 
                       &audioData->samples[audioData->currentFrame * audioData->channels], 
                       samplesToCopy * sizeof(int16_t));
            
            audioData->currentFrame += framesToCopy;
            
            // Nếu còn frame trống, điền silence
            if (framesToCopy < framesPerBuffer) {
                size_t silenceSamples = (framesPerBuffer - framesToCopy) * audioData->channels;
                std::memset(&output[samplesToCopy], 0, silenceSamples * sizeof(int16_t));
            }
            
            // Hiển thị progress
            static size_t lastReported = 0;
            if (audioData->currentFrame - lastReported > audioData->sampleRate) {
                double progress = 100.0 * audioData->currentFrame / audioData->totalFrames;
                std::cout << "🎵 Progress: " << std::fixed << std::setprecision(1) 
                         << progress << "%" << std::endl;
                lastReported = audioData->currentFrame;
            }
        } else {
            // Hết dữ liệu, điền silence
            std::memset(output, 0, framesPerBuffer * audioData->channels * sizeof(int16_t));
            std::cout << "🏁 Kết thúc phát nhạc" << std::endl;
            return paComplete;
        }
        
        return paContinue;
    }
    
public:
    WAVPlayer() : stream(nullptr), isInitialized(false) {
        audioData = std::make_unique<AudioData>();
    }
    
    ~WAVPlayer() {
        stop();
        terminate();
    }
    
    bool initialize() {
        PaError err = Pa_Initialize();
        if (err != paNoError) {
            std::cerr << "❌ Lỗi khởi tạo PortAudio: " << Pa_GetErrorText(err) << std::endl;
            return false;
        }
        isInitialized = true;
        std::cout << "✅ PortAudio đã khởi tạo" << std::endl;
        return true;
    }
    
    void terminate() {
        if (isInitialized) {
            Pa_Terminate();
            isInitialized = false;
        }
    }
    
    bool loadWAV(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "❌ Không thể mở file: " << filename << std::endl;
            return false;
        }
        
        // Đọc WAV header
        char header[44];
        file.read(header, 44);
        
        // Kiểm tra định dạng WAV
        if (std::strncmp(header, "RIFF", 4) != 0 || std::strncmp(header + 8, "WAVE", 4) != 0) {
            std::cerr << "❌ File không phải định dạng WAV hợp lệ" << std::endl;
            return false;
        }
        
        // Lấy thông tin từ header
        audioData->channels = *reinterpret_cast<int16_t*>(header + 22);
        audioData->sampleRate = static_cast<unsigned int>(*reinterpret_cast<int32_t*>(header + 24));
        int bitsPerSample = *reinterpret_cast<int16_t*>(header + 34);
        int dataSize = *reinterpret_cast<int32_t*>(header + 40);
        
        std::cout << "📊 Thông tin file WAV:" << std::endl;
        std::cout << "   🎵 Channels: " << audioData->channels << std::endl;
        std::cout << "   🔊 Sample Rate: " << audioData->sampleRate << " Hz" << std::endl;
        std::cout << "   🎚️  Bits per Sample: " << bitsPerSample << std::endl;
        std::cout << "   📏 Data Size: " << dataSize << " bytes" << std::endl;
        
        // Chỉ hỗ trợ 16-bit PCM
        if (bitsPerSample != 16) {
            std::cerr << "❌ Chỉ hỗ trợ file WAV 16-bit. File này là " << bitsPerSample << "-bit" << std::endl;
            return false;
        }
        
        // Tính số frame và đọc dữ liệu
        audioData->totalFrames = dataSize / (audioData->channels * 2);
        audioData->currentFrame = 0;
        audioData->samples.resize(dataSize / 2); // 2 bytes per sample
        
        file.read(reinterpret_cast<char*>(audioData->samples.data()), dataSize);
        file.close();
        
        double duration = static_cast<double>(audioData->totalFrames) / audioData->sampleRate;
        std::cout << "🔍 Tổng số frames: " << audioData->totalFrames << std::endl;
        std::cout << "🔍 Thời lượng: " << std::fixed << std::setprecision(2) << duration << " giây" << std::endl;
        std::cout << "✅ Load file WAV thành công!" << std::endl;
        
        return true;
    }
    
    bool play() {
        if (!isInitialized || audioData->samples.empty()) {
            std::cerr << "❌ Chưa khởi tạo hoặc chưa load file" << std::endl;
            return false;
        }
        
        // Cấu hình stream parameters
        PaStreamParameters outputParameters;
        outputParameters.device = Pa_GetDefaultOutputDevice();
        if (outputParameters.device == paNoDevice) {
            std::cerr << "❌ Không tìm thấy output device" << std::endl;
            return false;
        }
        
        outputParameters.channelCount = audioData->channels;
        outputParameters.sampleFormat = paInt16;
        outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
        outputParameters.hostApiSpecificStreamInfo = nullptr;
        
        // Mở stream
        PaError err = Pa_OpenStream(&stream,
                                   nullptr, // no input
                                   &outputParameters,
                                   audioData->sampleRate,
                                   256, // frames per buffer
                                   paClipOff,
                                   audioCallbackWrapper,
                                   this); // Pass 'this' as user data
        
        if (err != paNoError) {
            std::cerr << "❌ Lỗi mở stream: " << Pa_GetErrorText(err) << std::endl;
            return false;
        }
        
        // Bắt đầu phát
        err = Pa_StartStream(stream);
        if (err != paNoError) {
            std::cerr << "❌ Lỗi start stream: " << Pa_GetErrorText(err) << std::endl;
            Pa_CloseStream(stream);
            return false;
        }
        
        std::cout << "🎶 Đang phát nhạc... Nhấn Enter để dừng" << std::endl;
        
        // Chờ người dùng nhấn Enter hoặc nhạc phát hết
        while (Pa_IsStreamActive(stream)) {
            Pa_Sleep(100);
            
            // Kiểm tra input từ keyboard (đơn giản)
            // Trong thực tế có thể dùng thread riêng cho việc này
        }
        
        return true;
    }
    
    void stop() {
        if (stream) {
            Pa_StopStream(stream);
            Pa_CloseStream(stream);
            stream = nullptr;
        }
    }
    
    void printDevices() const {
        if (!isInitialized) return;
        
        int deviceCount = Pa_GetDeviceCount();
        std::cout << "🔍 Tìm thấy " << deviceCount << " audio devices:" << std::endl;
        
        for (int i = 0; i < deviceCount; i++) {
            const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
            std::cout << "   Device " << i << ": " << deviceInfo->name 
                     << " (max output channels: " << deviceInfo->maxOutputChannels << ")" << std::endl;
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cout << "📖 Cách sử dụng: " << argv[0] << " <file.wav>" << std::endl;
        std::cout << "   Ví dụ: " << argv[0] << " music.wav" << std::endl;
        return 1;
    }
    
    std::cout << "🎵 C++ PortAudio WAV Player" << std::endl;
    std::cout << "===========================" << std::endl;
    
    try {
        WAVPlayer player;
        
        if (!player.initialize()) {
            return 1;
        }
        
        player.printDevices();
        std::cout << std::endl;
        
        if (!player.loadWAV(argv[1])) {
            return 1;
        }
        
        if (!player.play()) {
            return 1;
        }
        
        std::cout << "✅ Hoàn tất!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Lỗi: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}