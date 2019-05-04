#pragma once

// LZ4 Headers
#include <lz4frame.h>

// Standard headers
#include <array>
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <streambuf>
#include <vector>

namespace lz4_stream
{
template<size_t SrcBufSize = 256>
class basic_ostream: public std::ostream
{
public:
	explicit basic_ostream(std::ostream &ioSink)
		: std::ostream{new output_buffer{ioSink}}
		, m_buffer(reinterpret_cast<output_buffer *>(rdbuf()))
	{
		assert(m_buffer);
	}

	~basic_ostream() final
	{
		this->Close();
		delete[] m_buffer;
	}

	void Close()
	{
		m_buffer->Close();
	}

private:
	class output_buffer: public std::streambuf
	{
	public:
		output_buffer(const output_buffer &) = delete;
		output_buffer &operator=(const output_buffer &) = delete;

		explicit output_buffer(std::ostream &sink)
			: m_sink{sink}
			// TODO: No need to recalculate the m_destinationBuffer size on each construction
			, m_destinationBuffer{LZ4F_compressBound(m_sourceBuffer.size(), nullptr)}
			, m_context{nullptr}
			, m_closed{false}
		{
			char const *const base = m_sourceBuffer.data();
			setp(base, base + m_sourceBuffer.size() - 1);

			std::size_t const result = LZ4F_createCompressionContext(&m_context, LZ4F_VERSION);
			if (LZ4F_isError(result) not_eq 0) {
				std::string error;
				error += "Failed to create LZ4 compression context: ";
				error += LZ4F_getErrorName(result);
				throw std::runtime_error{error};
			}
			this->WriteHeader();
		}

		~output_buffer() final
		{
			this->Close();
		}

		void Close()
		{
			if (m_closed)
				return;

			this->sync();
			this->WriteFooter();
			LZ4F_freeCompressionContext(m_context);
			m_closed = true;
		}

	private:
		int_type overflow(int_type iCharacter) final
		{
			assert(std::less_equal<char *>()(pptr(), epptr()));

			*pptr() = static_cast<basic_ostream::char_type>(iCharacter);
			pbump(1);
			this->CompressAndWrite();

			return iCharacter;
		}

		int_type sync() final
		{
			this->CompressAndWrite();
			return 0;
		}

		void CompressAndWrite()
		{
			// TODO: Throw exception instead or set badbit
			assert(!m_closed);
			auto const orig_size = static_cast<std::int32_t>(pptr() - pbase());
			pbump(-orig_size);
			std::size_t const result = LZ4F_compressUpdate(
				m_context,
				m_destinationBuffer.data(),
				m_destinationBuffer.capacity(),
				pbase(),
				orig_size,
				nullptr
			);

			if (LZ4F_isError(result) not_eq 0) {
				std::string error;
				error += "LZ4 compression failed: ";
				error += LZ4F_getErrorName(result);
				throw std::runtime_error{error};
			}

			m_sink.write(m_destinationBuffer.data(), result);
		}

		void WriteHeader()
		{
			// TODO: Throw exception instead or set badbit
			assert(!m_closed);
			std::size_t const result = LZ4F_compressBegin(
				m_context,
				m_destinationBuffer.data(),
				m_destinationBuffer.capacity(),
				nullptr
			);

			if (LZ4F_isError(result) not_eq 0) {
				std::string error;
				error += "Failed to start LZ4 compression: ";
				error += LZ4F_getErrorName(result);
				throw std::runtime_error{error};
			}
			m_sink.write(m_destinationBuffer.data(), result);
		}

		void WriteFooter()
		{
			assert(!m_closed);
			std::size_t const result = LZ4F_compressEnd(
				m_context,
				m_destinationBuffer.data(),
				m_destinationBuffer.capacity(),
				nullptr
			);
			if (LZ4F_isError(result) not_eq 0) {
				std::string error;
				error += "Failed to end LZ4 compression: ";
				error += LZ4F_getErrorName(result);
				throw std::runtime_error{error};
			}
			m_sink.write(m_destinationBuffer.data(), result);
		}

		std::ostream &m_sink;
		std::array<char, SrcBufSize> m_sourceBuffer;
		std::vector<char> m_destinationBuffer;
		LZ4F_compressionContext_t m_context;
		bool m_closed;
	};

	output_buffer *m_buffer;
};

template<size_t SrcBufSize = 256, size_t DestBufSize = 256>
class basic_istream: public std::istream
{
public:
	explicit basic_istream(std::istream &iSource)
		: std::istream{new input_buffer(iSource)}
		, m_buffer{static_cast<input_buffer *>(rdbuf())}
	{
		assert(m_buffer);
	}

	~basic_istream() final
	{
		delete m_buffer;
	}

private:
	class input_buffer: public std::streambuf
	{
	public:
		input_buffer(std::istream & iSource)
			: m_source{iSource}
			, m_offset{0}
			, m_sourceBufferSize{0}
			, m_context{nullptr}
		{
			std::size_t const result = LZ4F_createDecompressionContext(&m_context, LZ4F_VERSION);
			if (LZ4F_isError(result) not_eq 0) {
				std::string error;
				error += "Failed to create LZ4 decompression context: ";
				error += LZ4F_getErrorName(result);
				throw std::runtime_error{error};
			}
			setg(sourceBuffer.data(), sourceBuffer.data(), sourceBuffer.data());
		}

		~input_buffer() final
		{
			LZ4F_freeDecompressionContext(m_context);
		}

		int_type underflow() final
		{
			std::size_t writtenSize = 0;
			while (writtenSize == 0) {
				if (m_offset == m_sourceBufferSize) {
					m_source.read(sourceBuffer.data(), sourceBuffer.size());
					m_sourceBufferSize = static_cast<std::size_t>(m_source.gcount());
					m_offset = 0;
				}

				if (m_sourceBufferSize == 0) {
					return traits_type::eof();
				}

				std::size_t const src_size = m_sourceBufferSize - m_offset;
				std::size_t const dest_size = destinationBuffer.size();
				std::size_t const result = LZ4F_decompress(
					m_context,
					destinationBuffer.data(),
					&dest_size,
					sourceBuffer.data() + m_offset,
					&src_size,
					nullptr
				);

				if (LZ4F_isError(result) != 0) {
					std::string error;
					error += "LZ4 decompression failed: ";
					error += LZ4F_getErrorName(result);
					throw std::runtime_error{error};
				}
				writtenSize = dest_size;
				m_offset += src_size;
			}

			setg(destinationBuffer.data(), destinationBuffer.data(), destinationBuffer.data() + writtenSize);
			return traits_type::to_int_type(*gptr());
		}

		input_buffer(const input_buffer &) = delete;
		input_buffer &operator=(const input_buffer &) = delete;

	private:
		std::istream &m_source;
		std::array<char, SrcBufSize> sourceBuffer;
		std::array<char, DestBufSize> destinationBuffer;
		size_t m_offset;
		size_t m_sourceBufferSize;
		LZ4F_decompressionContext_t m_context;
	};

	input_buffer *m_buffer;
};

using ostream = basic_ostream<>;
using istream = basic_istream<>;

}
