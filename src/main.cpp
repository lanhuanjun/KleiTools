#include "tex_file.h"
#include <iostream>
#include <fstream>
#include <filesystem>

#include <zip.h>
#include <zipconf.h>
#include <compressonator.h>

namespace fs = std::filesystem;
char* g_buf = nullptr;
size_t g_buf_size = 0;
void BatchConvert(const fs::path& src, const fs::path& dst, KleiPixelFormat pf = ARGB, bool saveToPng = false)
{
	if (!fs::exists(src))
	{
		std::println(std::cerr, "{} no exists!", src.string());
		return;
	}
	for (auto& it : fs::directory_iterator(src))
	{
		if (it.is_directory())
		{
			const auto targetDir = std::format("{}\\{}", dst.string(), it.path().filename().string());
			fs::create_directories(targetDir);
			BatchConvert(it.path(), targetDir, pf, saveToPng);
		}
		else
		{
			const auto& ext = it.path().filename().extension().string();
			const auto originFile = it.path().string();
			const auto targetFile = std::format("{}\\{}", dst.string(), it.path().filename().string());
			if (ext == ".tex")
			{
				TexFile texFile(it.path(), g_buf, g_buf_size);
				texFile.Load();
				if (texFile.Convert(targetFile, pf))
				{
					std::println(std::cout, "yes:{}", originFile);
				}
				else
				{
					std::println(std::cout, "no:{}", originFile);
				}
			}
			else if (ext == ".zip")
			{
				fs::copy_file(originFile, targetFile);
				// 打开解压文件
				int iErr = 0;
				zip_t* originZip = zip_open(originFile.c_str(), ZIP_CHECKCONS, &iErr);
				if (!originZip)
				{
					zip_error_t error;
					zip_error_init_with_code(&error, iErr);
					std::println(std::cerr, "{} open fail. code:{}, err:{}", originFile, iErr, zip_error_strerror(&error));
					zip_error_fini(&error);
					continue;
				}
				zip_t* targetZip = zip_open(targetFile.c_str(), 0, &iErr);
				if (!targetZip)
				{
					zip_error_t error;
					zip_error_init_with_code(&error, iErr);
					std::println(std::cerr, "{} open fail. code:{}, err:{}", targetFile, iErr, zip_error_strerror(&error));
					zip_error_fini(&error);
					continue;
				}

				// 遍历压缩文件
				struct zip_stat stat;
				const zip_int64_t i64Num = zip_get_num_entries(originZip, 0);
				const std::string tempDir = targetFile.substr(0, targetFile.length() - 4);
				fs::create_directories(tempDir);
				for (zip_int64_t i = 0; i < i64Num; ++i)
				{
					zip_stat_index(originZip, i, 0, &stat);
					zip_file_t* zipIndex = zip_fopen_index(originZip, i, 0);
					if (!zipIndex)
					{
						std::println(std::cerr, "zip open index [{}] fail!", i);
						break;
					}
					// 非tex
					if (std::strcmp(stat.name + (std::strlen(stat.name) - 3), "tex") != 0)
					{
						continue;
					}

					const auto unzipTexFile = std::format("{}\\{}", tempDir, stat.name);
					std::ofstream unzipStream(unzipTexFile, std::ios::binary | std::ios::trunc);
					zip_int64_t nRead = 0;
					while ((nRead = zip_fread(zipIndex, g_buf, g_buf_size)) > 0)
					{
						unzipStream.write(g_buf, nRead);
					}
					unzipStream.flush();
					unzipStream.close();
					TexFile texFile(unzipTexFile, g_buf, g_buf_size);
					texFile.Load();
					if (!texFile.Convert(unzipTexFile, pf))
					{
						std::println(std::cerr, "zip [{}] convert [{}] fail!", src.string(), stat.name);
						break;
					}
					zip_source_t* srcFile = zip_source_file(targetZip, unzipTexFile.c_str(), 0, ZIP_LENGTH_TO_END);
					if (!srcFile)
					{
						std::println(std::cerr, "zip [{}] add [{}] fail!", src.string(), stat.name);
						break;
					}
					if (zip_file_add(targetZip, stat.name, srcFile, ZIP_FL_OVERWRITE | ZIP_FL_ENC_RAW) < 0)
					{
						zip_source_free(srcFile);
					}
				}
				zip_close(originZip);
				zip_close(targetZip);
				fs::remove_all(tempDir);
			}
			else
			{
				std::println(std::cout, "ignore:{}", originFile);
				fs::copy_file(originFile, targetFile);
			}
		}
	}
}

const char* g_usage = "kleitool.exe -i xxx[file|dir]  -o xxx[file|dir] -t [0:DTX1, 1:DTX3 2:DTX5 4:ARGB 5:RGB 18:ETC2_EAC] -p [dir, output to png]";

std::string g_src;
std::string g_dst;
std::string g_png_dir;
KleiPixelFormat g_pixel_format = UNKNOWN_PIXEL_FORMAT;

bool ParseCommand(int argc, char* argv[])
{
	if (argc < 2)
	{
		return false;
	}

	for (int i = 1; i < argc; ++i)
	{
		if (std::strcmp(argv[i], "-i") == 0)
		{
			if (i < argc)
			{
				g_src = argv[++i];
			}
			else
			{
				return false;
			}
		}
		else if (std::strcmp(argv[i], "-o") == 0)
		{
			if (i < argc)
			{
				g_dst = argv[++i];
			}
			else
			{
				return false;
			}
		}
		else if (std::strcmp(argv[i], "-t") == 0)
		{
			if (i < argc)
			{
				const uint32_t type = std::stoi(argv[++i]);
				if (type != DXT1 && type != DXT3 && type != DXT5 && type != ARGB && type != RGB && type != ETC2_EAC)
				{
					return false;
				}
				g_pixel_format = (KleiPixelFormat)type;
			}
			else
			{
				return false;
			}
		}
		else if (std::strcmp(argv[i], "-p") == 0)
		{
			if (i < argc)
			{
				g_png_dir = argv[++i];
			}
			else
			{
				return false;
			}
		}
	}

	return fs::exists(g_src) && !g_dst.empty() && g_pixel_format != UNKNOWN_PIXEL_FORMAT;
}

int main(int argc, char* argv[])
{
	if (!ParseCommand(argc, argv))
	{
		std::println(std::cerr, "{}", g_usage);
		return -1;
	}
	try
	{
		CMP_InitFramework();
		g_buf_size = 100 * 1024 * 1024;
		g_buf = new char[g_buf_size];
		if (fs::is_directory(g_src))
		{
			BatchConvert(g_src, g_dst, g_pixel_format);
		}
		else
		{
			TexFile texFile(g_src, g_buf, g_buf_size);
			texFile.Load();
			if (!g_png_dir.empty())
			{
				if (!fs::exists(g_png_dir))
				{
					fs::create_directories(g_png_dir);
				}
				texFile.SaveToPng(g_png_dir);
			}
			if (!texFile.Convert(g_dst, g_pixel_format))
			{
				std::println(std::cerr, "convert error!");
			}
		}
		delete[] g_buf;
		g_buf_size = 0;
	}
	catch (const std::exception& e)
	{
		delete[] g_buf;
		g_buf_size = 0;
		std::println(std::cerr, "{}", e.what());
	}
	
	return 0;
}
