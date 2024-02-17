#include "tex_file.h"

#include <fstream>
#include <opencv2/opencv.hpp>
#include <compressonator.h>
#include <squish.h>



TexFile::TexFile(std::filesystem::path path, char* pBuf, size_t bufSize)
	: m_path(std::move(path)), m_buf(pBuf), m_buf_size(bufSize)
{

}

TexFile::~TexFile()
{

}

bool TexFile::Load()
{
	std::ifstream stream(m_path, std::ios::binary | std::ios::in);
	if (!stream.is_open())
	{
		std::println(std::cerr, "{} can't open!", m_path.string());
		stream.close();
		return false;
	}
	std::println(std::cout, "convert {}", m_path.string());
	// 读取魔术字
	char buf[4] = {0};
	if (!stream.read(buf, 4))
	{
		std::println(std::cerr, "{} read magic fail!", m_path.string());
		stream.close();
		return false;
	}
	if (KTEX_HEADER != std::string_view(buf, 4))
	{
		std::println(std::cerr, "The first 4 bytes [{}] do not match 'KTEX'", std::string_view(buf, 4));
		stream.close();
		return false;
	}
	// 读取文件头和MipMaps
	if (!ReadHeader(stream) || !ReadMipMaps(stream))
	{
		stream.close();
		return false;
	}
	stream.close();
	return true;
}


bool TexFile::IsPreCaveUpdate() const
{
	return m_header.old_remainder == 262143;
}

bool TexFile::Convert(const std::string& savePath, KleiPixelFormat pf) const
{
	auto targetPixelFormat = GetThirdPixelFormat(pf);
	assert(targetPixelFormat.has_value());
	std::ofstream stream(savePath, std::ios::trunc | std::ios::binary);
	if (!stream.is_open())
	{
		stream.close();
		std::println(std::cerr, "open {} fail!", savePath);
		return false;
	}
	// 写入魔术字
	stream.write(KTEX_HEADER.data(), 4);
	
	// 写入文件头
	SaveHeader(stream, pf);

	CMP_Texture srcTexture, destTexture;
	// 写入mipmaps
	for (auto it = m_mip_maps.begin(); it != m_mip_maps.end(); ++it)
	{
		destTexture.dwSize = sizeof(CMP_Texture);
		destTexture.dwWidth = it->width;
		destTexture.dwHeight = it->height;
		destTexture.dwPitch = 0;
		destTexture.format = targetPixelFormat.value();
		uint32_t size = destTexture.dwDataSize = CMP_CalculateBufferSize(&destTexture);

		assert(size < m_buf_size);

		char* p = m_buf;
		std::memcpy(p, &(it->width), sizeof(it->width));
		p += sizeof(it->width);
		std::memcpy(p, &(it->height), sizeof(it->height));
		p += sizeof(it->height);
		std::memcpy(p, &(it->pitch), sizeof(it->pitch));
		p += sizeof(it->pitch);
		std::memcpy(p, &size, sizeof(size));
		stream.write(m_buf, 10);
	}
	CMP_CompressOptions options = { 0 };
	options.dwSize = sizeof(options);
	options.fquality = 1;
	for (auto it = m_mip_maps.begin(); it != m_mip_maps.end(); ++it)
	{
		srcTexture.dwSize = sizeof(CMP_Texture);
		srcTexture.dwWidth = it->width;
		srcTexture.dwHeight = it->height;
		srcTexture.dwPitch = it->pitch;
		srcTexture.format = m_third_pixel_format;
		srcTexture.dwDataSize = it->size;
		srcTexture.pData = it->data;

		destTexture.dwSize = sizeof(CMP_Texture);
		destTexture.dwWidth = it->width;
		destTexture.dwHeight = it->height;
		destTexture.dwPitch = 0;
		destTexture.format = targetPixelFormat.value();
		destTexture.dwDataSize = CMP_CalculateBufferSize(&destTexture);
		destTexture.pData = (uint8_t*)m_buf;

		CMP_ERROR cmpStatus = CMP_ConvertTexture(&srcTexture, &destTexture, &options, nullptr);
		if (cmpStatus != CMP_OK)
		{
			std::println(std::cerr, "Compression returned an error {}", (int)cmpStatus);
			return false;
		}
		stream.write((char*)destTexture.pData, destTexture.dwDataSize);
	}
	
	stream.flush();
	stream.close();
	return true;
}

void TexFile::SaveToPng(const std::string& dir) const
{
	if (m_mip_maps.empty())
	{
		std::println(std::cout, "mipmap is empty!");
		return;
	}
	for (auto it = m_mip_maps.begin(); it != m_mip_maps.end(); ++it)
	{
		const auto& name = m_path.filename().string();
		SaveOneMipMapToPng(std::format("{}\\{}-{}x{}.png", dir, name.substr(0, name.length() - 4), it->width, it->height),*it);
	}
}

bool TexFile::ReadHeader(std::ifstream& stream)
{
	// 读取文件头
	uint32_t head = 0;
	if (!stream.read(reinterpret_cast<char*>(&head), sizeof(head)))
	{
		std::println(std::cerr, "read header fail!");
		return false;
	}
	m_header.platform = head & 15;
	m_header.pixel_format = (head >> 4) & 31;
	m_header.texture_type = (head >> 9) & 15;
	m_header.num_mips = (head >> 13) & 31;
	m_header.flags = (head >> 18) & 3;
	m_header.remainder = (head >> 20) & 4095;
	// 保留
	m_header.old_remainder = (head >> 14) & 262143;

	m_header.origin = head;
	std::println(std::cout, "tex info. plat:{}, pixel:{}, text:{}, numMips: {}, flags:{}, remainder:{}, old_remainder:{}", 
		m_header.platform, m_header.pixel_format, m_header.texture_type, m_header.num_mips, m_header.flags, m_header.remainder,
		m_header.old_remainder);
	const auto pixel = GetThirdPixelFormat(m_header.pixel_format);
	assert(pixel.has_value());
	m_third_pixel_format = pixel.value();
	return true;
}

bool TexFile::ReadMipMaps(std::ifstream& stream)
{
	// 读取图片信息
	for (uint32_t i = 0; i < m_header.num_mips; ++i)
	{
		if (!stream.read(m_buf, 10))
		{
			std::println(std::cerr, "read mipmaps fail! index: {}", i + 1);
			return false;
		}
		auto& mipMap = this->m_mip_maps.emplace_back();
		const char* p = m_buf;
		std::memcpy(&(mipMap.width), p, sizeof(mipMap.width));
		p += sizeof(mipMap.width);
		std::memcpy(&(mipMap.height), p, sizeof(mipMap.height));
		p += sizeof(mipMap.height);
		std::memcpy(&(mipMap.pitch), p, sizeof(mipMap.pitch));
		p += sizeof(mipMap.pitch);
		std::memcpy(&(mipMap.size), p, sizeof(mipMap.size));

		std::println(std::cout, "mipmap. width:{} x height:{}, pitch:{}, size:{}",
			mipMap.width, mipMap.height, mipMap.pitch, mipMap.size);
	}
	// 读取图片数据
	uint32_t needRead = m_buf_size;
	uint32_t readSize = 0;
	for(auto it = m_mip_maps.begin(); it != m_mip_maps.end(); ++it)
	{
		readSize = 0;
		uint8_t* p = it->data = new uint8_t[it->size] {0};
		while (!stream.eof() && it->size > readSize)
		{
			if (it->size - readSize < m_buf_size)
			{
				needRead = it->size - readSize;
			}
			stream.read(m_buf, needRead);
			std::memcpy(p + readSize, m_buf, stream.gcount());
			readSize += stream.gcount();
		}
		if (readSize != it->size)
		{
			std::println(std::cerr, "read mipmap error! readSize:{} need:{}", readSize, it->size);
		}
	}
	
	return true;
}

void TexFile::SaveOneMipMapToPng(std::string path, const MipMap& mipMap) const
{
	CMP_Texture srcTexture, destTexture;
	srcTexture.dwSize = sizeof(CMP_Texture);
	srcTexture.dwWidth = mipMap.width;
	srcTexture.dwHeight = mipMap.height;
	srcTexture.dwPitch = mipMap.pitch;
	srcTexture.format = m_third_pixel_format;
	srcTexture.dwDataSize = mipMap.size;
	srcTexture.pData = mipMap.data;

	destTexture.dwSize = sizeof(CMP_Texture);
	destTexture.dwWidth = mipMap.width;
	destTexture.dwHeight = mipMap.height;
	destTexture.dwPitch = 0;
	destTexture.format = GetThirdPixelFormat(ARGB).value();
	destTexture.dwDataSize = CMP_CalculateBufferSize(&destTexture);
	destTexture.pData = (uint8_t*)m_buf;
	assert(m_buf_size > destTexture.dwDataSize);

	CMP_CompressOptions options = { 0 };
	options.dwSize = sizeof(options);
	options.fquality = 1;
	CMP_ERROR cmpStatus = CMP_ConvertTexture(&srcTexture, &destTexture, &options, nullptr);
	if (cmpStatus != CMP_OK)
	{
		std::println(std::cerr, "Compression returned an error {}", (int)cmpStatus);
		return;
	}

	cv::Mat rawPng(mipMap.height, mipMap.width, CV_8UC4);
	int index = 0;
	for (int i = 0; i < rawPng.rows; ++i)
	{
		for (int j = 0; j < rawPng.cols; ++j)
		{
			cv::Vec4b& rgba = rawPng.at<cv::Vec4b>(i, j);
			rgba[2] = destTexture.pData[index++]; // blue
			rgba[1] = destTexture.pData[index++]; // green
			rgba[0] = destTexture.pData[index++]; // red
			rgba[3] = destTexture.pData[index++]; // alpha
		}
	}
	std::vector<int> params;
	params.push_back(cv::IMWRITE_PNG_COMPRESSION);
	params.push_back(0);

	cv::imwrite(path, rawPng, params);
}

void TexFile::SaveHeader(std::ofstream& stream, KleiPixelFormat pf) const
{
	uint32_t head = 0;
	head |= 4095;
	head <<= 2;
	head |= m_header.flags;
	head <<= 5;
	head |= m_header.num_mips;
	head <<= 4;
	head |= m_header.texture_type;
	head <<= 5;
	head |= static_cast<uint32_t>(pf);
	head <<= 4;
	head |= m_header.platform;
	stream.write(reinterpret_cast<char*>(&head), sizeof(head));
}

std::optional<CMP_FORMAT> TexFile::GetThirdPixelFormat(uint32_t pixelFormat) const
{
	switch (pixelFormat)
	{
		case DXT1:
			return CMP_FORMAT_DXT1;
		case DXT3:
			return CMP_FORMAT_DXT3;
		case DXT5:
			return CMP_FORMAT_DXT5;
		case RGB:
			return CMP_FORMAT_RGB_888;
		case ARGB:
			return CMP_FORMAT_ARGB_8888;
		case ETC2_EAC:
			return CMP_FORMAT_ETC2_RGBA;
		default:
			std::println(std::cerr, "nonsupport pixel: {}", pixelFormat);
			break;
	}
	return std::nullopt;
}
