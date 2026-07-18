#include "Platform/Windows/MediaFoundationDecoder.h"

#include <cstring>
#include <limits>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <objbase.h>
#include <string>
#include <utility>
#include <windows.h>

namespace aengine::detail
{
    namespace
    {
        constexpr auto OutputSampleRate = 48000U;
        constexpr auto OutputChannelCount = 2U;
        constexpr auto FirstAudioStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);

        template<typename T>
        void ReleaseComObject(T*& value)
        {
            if (value != nullptr)
            {
                value->Release();
                value = nullptr;
            }
        }

        Error MakeMediaFoundationError(ErrorCode code, std::string operation, HRESULT result)
        {
            operation.append(" failed with HRESULT ");
            operation.append(std::to_string(static_cast<long>(result)));
            operation.push_back('.');
            return Error(code, std::move(operation));
        }

        Expected<std::wstring> Utf8ToWide(std::string_view value)
        {
            if (value.empty())
            {
                return std::unexpected(Error(ErrorCode::InvalidArgument, "The audio source path is empty."));
            }

            if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            {
                return std::unexpected(Error(ErrorCode::InvalidArgument, "The audio source path is too long."));
            }

            auto requiredSize = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0);
            if (requiredSize <= 0)
            {
                return std::unexpected(Error(ErrorCode::InvalidArgument, "The audio source path is not valid UTF-8."));
            }

            auto result = std::wstring(static_cast<std::size_t>(requiredSize), L'\0');
            auto convertedSize = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                result.data(),
                requiredSize);
            if (convertedSize != requiredSize)
            {
                return std::unexpected(Error(ErrorCode::InvalidArgument, "The audio source path could not be converted to UTF-16."));
            }

            return result;
        }

        Expected<IMFSourceReader*> CreateSourceReaderFromFile(const AudioSource& source)
        {
            auto widePath = Utf8ToWide(source.FilePath);
            if (!widePath)
            {
                return std::unexpected(std::move(widePath.error()));
            }

            auto sourceReader = static_cast<IMFSourceReader*>(nullptr);
            auto result = MFCreateSourceReaderFromURL(widePath->c_str(), nullptr, &sourceReader);
            if (FAILED(result) || sourceReader == nullptr)
            {
                ReleaseComObject(sourceReader);
                return std::unexpected(MakeMediaFoundationError(
                    ErrorCode::ResourceLoadFailed,
                    "Media Foundation could not open the audio file",
                    result));
            }

            return sourceReader;
        }

        Expected<IMFSourceReader*> CreateSourceReaderFromMemory(const AudioSource& source)
        {
            if (source.EncodedBytes.empty())
            {
                return std::unexpected(Error(ErrorCode::InvalidArgument, "The memory audio source is empty."));
            }

            auto globalMemory = GlobalAlloc(GMEM_MOVEABLE, source.EncodedBytes.size());
            if (globalMemory == nullptr)
            {
                return std::unexpected(Error(ErrorCode::ResourceLoadFailed, "Could not allocate memory for the encoded audio source."));
            }

            auto memory = GlobalLock(globalMemory);
            if (memory == nullptr)
            {
                GlobalFree(globalMemory);
                return std::unexpected(Error(ErrorCode::ResourceLoadFailed, "Could not lock memory for the encoded audio source."));
            }

            std::memcpy(memory, source.EncodedBytes.data(), source.EncodedBytes.size());
            GlobalUnlock(globalMemory);

            auto stream = static_cast<IStream*>(nullptr);
            auto result = CreateStreamOnHGlobal(globalMemory, TRUE, &stream);
            if (FAILED(result) || stream == nullptr)
            {
                if (stream != nullptr)
                {
                    ReleaseComObject(stream);
                }
                else
                {
                    GlobalFree(globalMemory);
                }

                return std::unexpected(MakeMediaFoundationError(
                    ErrorCode::ResourceLoadFailed,
                    "Could not create a stream for the encoded audio source",
                    result));
            }

            auto byteStream = static_cast<IMFByteStream*>(nullptr);
            result = MFCreateMFByteStreamOnStreamEx(stream, &byteStream);
            ReleaseComObject(stream);
            if (FAILED(result) || byteStream == nullptr)
            {
                ReleaseComObject(byteStream);
                return std::unexpected(MakeMediaFoundationError(
                    ErrorCode::ResourceLoadFailed,
                    "Could not create a Media Foundation byte stream",
                    result));
            }

            auto sourceReader = static_cast<IMFSourceReader*>(nullptr);
            result = MFCreateSourceReaderFromByteStream(byteStream, nullptr, &sourceReader);
            ReleaseComObject(byteStream);
            if (FAILED(result) || sourceReader == nullptr)
            {
                ReleaseComObject(sourceReader);
                return std::unexpected(MakeMediaFoundationError(
                    ErrorCode::ResourceLoadFailed,
                    "Media Foundation could not open the encoded audio source",
                    result));
            }

            return sourceReader;
        }

        Expected<IMFSourceReader*> CreateSourceReader(const AudioSource& source)
        {
            if (source.Kind == AudioSourceKind::File)
            {
                return CreateSourceReaderFromFile(source);
            }

            if (source.Kind == AudioSourceKind::Memory)
            {
                return CreateSourceReaderFromMemory(source);
            }

            return std::unexpected(Error(ErrorCode::InvalidArgument, "The audio source kind is invalid."));
        }

        Result ConfigureOutputType(IMFSourceReader& sourceReader, PcmSampleFormat sampleFormat)
        {
            auto mediaType = static_cast<IMFMediaType*>(nullptr);
            auto result = MFCreateMediaType(&mediaType);
            if (FAILED(result) || mediaType == nullptr)
            {
                ReleaseComObject(mediaType);
                return std::unexpected(MakeMediaFoundationError(
                    ErrorCode::DecodeFailed,
                    "Could not create the decoded PCM media type",
                    result));
            }

            auto bitsPerSample = sampleFormat == PcmSampleFormat::Float32 ? 32U : 16U;
            auto bytesPerSample = bitsPerSample / 8U;
            auto blockAlignment = OutputChannelCount * bytesPerSample;
            result = mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            if (SUCCEEDED(result))
            {
                result = mediaType->SetGUID(
                    MF_MT_SUBTYPE,
                    sampleFormat == PcmSampleFormat::Float32 ? MFAudioFormat_Float : MFAudioFormat_PCM);
            }

            if (SUCCEEDED(result))
            {
                result = mediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, OutputChannelCount);
            }

            if (SUCCEEDED(result))
            {
                result = mediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, OutputSampleRate);
            }

            if (SUCCEEDED(result))
            {
                result = mediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, bitsPerSample);
            }

            if (SUCCEEDED(result))
            {
                result = mediaType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, blockAlignment);
            }

            if (SUCCEEDED(result))
            {
                result = mediaType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, OutputSampleRate * blockAlignment);
            }

            if (SUCCEEDED(result))
            {
                result = sourceReader.SetCurrentMediaType(FirstAudioStream, nullptr, mediaType);
            }

            ReleaseComObject(mediaType);
            if (FAILED(result))
            {
                return std::unexpected(MakeMediaFoundationError(
                    ErrorCode::DecodeFailed,
                    "Could not configure the decoded PCM format",
                    result));
            }

            return {};
        }

        Result ValidateOutputType(IMFSourceReader& sourceReader, PcmSampleFormat sampleFormat)
        {
            auto mediaType = static_cast<IMFMediaType*>(nullptr);
            auto result = sourceReader.GetCurrentMediaType(FirstAudioStream, &mediaType);
            if (FAILED(result) || mediaType == nullptr)
            {
                ReleaseComObject(mediaType);
                return std::unexpected(MakeMediaFoundationError(
                    ErrorCode::DecodeFailed,
                    "Could not query the decoded PCM format",
                    result));
            }

            auto subtype = GUID();
            auto channels = UINT32();
            auto sampleRate = UINT32();
            auto bitsPerSample = UINT32();
            result = mediaType->GetGUID(MF_MT_SUBTYPE, &subtype);
            if (SUCCEEDED(result))
            {
                result = mediaType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
            }

            if (SUCCEEDED(result))
            {
                result = mediaType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate);
            }

            if (SUCCEEDED(result))
            {
                result = mediaType->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bitsPerSample);
            }

            ReleaseComObject(mediaType);
            auto expectedSubtype = sampleFormat == PcmSampleFormat::Float32 ? MFAudioFormat_Float : MFAudioFormat_PCM;
            auto expectedBitsPerSample = sampleFormat == PcmSampleFormat::Float32 ? 32U : 16U;
            if (FAILED(result)
                || subtype != expectedSubtype
                || channels != OutputChannelCount
                || sampleRate != OutputSampleRate
                || bitsPerSample != expectedBitsPerSample)
            {
                return std::unexpected(Error(
                    ErrorCode::DecodeFailed,
                    "Media Foundation did not provide the requested 48 kHz stereo PCM format."));
            }

            return {};
        }

        Result AppendSample(IMFSample& sample, std::vector<std::byte>& samples)
        {
            auto mediaBuffer = static_cast<IMFMediaBuffer*>(nullptr);
            auto result = sample.ConvertToContiguousBuffer(&mediaBuffer);
            if (FAILED(result) || mediaBuffer == nullptr)
            {
                ReleaseComObject(mediaBuffer);
                return std::unexpected(MakeMediaFoundationError(
                    ErrorCode::DecodeFailed,
                    "Could not access a decoded PCM sample",
                    result));
            }

            auto data = static_cast<BYTE*>(nullptr);
            auto maximumLength = DWORD();
            auto currentLength = DWORD();
            result = mediaBuffer->Lock(&data, &maximumLength, &currentLength);
            if (FAILED(result))
            {
                ReleaseComObject(mediaBuffer);
                return std::unexpected(MakeMediaFoundationError(
                    ErrorCode::DecodeFailed,
                    "Could not lock a decoded PCM sample",
                    result));
            }

            if (currentLength > 0U && data == nullptr)
            {
                mediaBuffer->Unlock();
                ReleaseComObject(mediaBuffer);
                return std::unexpected(Error(ErrorCode::DecodeFailed, "A decoded PCM sample contained no data pointer."));
            }

            if (currentLength > 0U)
            {
                auto first = reinterpret_cast<const std::byte*>(data);
                samples.insert(samples.end(), first, first + currentLength);
            }

            result = mediaBuffer->Unlock();
            ReleaseComObject(mediaBuffer);
            if (FAILED(result))
            {
                return std::unexpected(MakeMediaFoundationError(
                    ErrorCode::DecodeFailed,
                    "Could not unlock a decoded PCM sample",
                    result));
            }

            return {};
        }
    }

    DecodedPcm::DecodedPcm()
        : Samples()
        , SampleRate(0U)
        , Channels(0U)
        , BitsPerSample(0U)
    {
    }

    Expected<DecodedPcm> DecodeWithMediaFoundation(const AudioSource& source, PcmSampleFormat sampleFormat)
    {
        auto sourceReaderResult = CreateSourceReader(source);
        if (!sourceReaderResult)
        {
            return std::unexpected(std::move(sourceReaderResult.error()));
        }

        auto sourceReader = *sourceReaderResult;
        auto configuration = ConfigureOutputType(*sourceReader, sampleFormat);
        if (!configuration)
        {
            ReleaseComObject(sourceReader);
            return std::unexpected(std::move(configuration.error()));
        }

        auto formatValidation = ValidateOutputType(*sourceReader, sampleFormat);
        if (!formatValidation)
        {
            ReleaseComObject(sourceReader);
            return std::unexpected(std::move(formatValidation.error()));
        }

        auto decoded = DecodedPcm();
        decoded.SampleRate = OutputSampleRate;
        decoded.Channels = static_cast<std::uint16_t>(OutputChannelCount);
        decoded.BitsPerSample = sampleFormat == PcmSampleFormat::Float32 ? 32U : 16U;

        while (true)
        {
            auto actualStreamIndex = DWORD();
            auto flags = DWORD();
            auto timestamp = LONGLONG();
            auto sample = static_cast<IMFSample*>(nullptr);
            auto result = sourceReader->ReadSample(
                FirstAudioStream,
                0U,
                &actualStreamIndex,
                &flags,
                &timestamp,
                &sample);
            if (FAILED(result))
            {
                ReleaseComObject(sample);
                ReleaseComObject(sourceReader);
                return std::unexpected(MakeMediaFoundationError(
                    ErrorCode::DecodeFailed,
                    "Could not read a decoded PCM sample",
                    result));
            }

            if ((flags & MF_SOURCE_READERF_ERROR) != 0U)
            {
                ReleaseComObject(sample);
                ReleaseComObject(sourceReader);
                return std::unexpected(Error(ErrorCode::DecodeFailed, "Media Foundation reported an error while decoding audio."));
            }

            if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) != 0U)
            {
                auto validation = ValidateOutputType(*sourceReader, sampleFormat);
                if (!validation)
                {
                    ReleaseComObject(sample);
                    ReleaseComObject(sourceReader);
                    return std::unexpected(std::move(validation.error()));
                }
            }

            if (sample != nullptr)
            {
                auto append = AppendSample(*sample, decoded.Samples);
                ReleaseComObject(sample);
                if (!append)
                {
                    ReleaseComObject(sourceReader);
                    return std::unexpected(std::move(append.error()));
                }
            }

            if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0U)
            {
                break;
            }
        }

        ReleaseComObject(sourceReader);
        if (decoded.Samples.empty())
        {
            return std::unexpected(Error(ErrorCode::DecodeFailed, "The decoded audio source contains no PCM samples."));
        }

        auto bytesPerFrame = static_cast<std::size_t>(decoded.Channels) * (decoded.BitsPerSample / 8U);
        if (decoded.Samples.size() % bytesPerFrame != 0U)
        {
            return std::unexpected(Error(ErrorCode::DecodeFailed, "The decoded PCM byte count is not frame-aligned."));
        }

        return decoded;
    }
}
