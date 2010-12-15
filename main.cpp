#include<cstdio>
#include<cstdlib>
#include<cstring>
#include<iomanip>
#include<iostream>
#include<vector>

using namespace std;

#include<stdint.h>
#include<unistd.h>
#include<sys/stat.h>
#include<sys/types.h>

extern "C" void lh5Decode(FILE *inputfile, FILE *outputfile, uint32_t originalsize, uint32_t compressedsize);

#define DATABUF_SIZE 8192

bool verbose = false;

bool fileExists(const char *pathname)
{
	struct stat stFileInfo;
	int intStat;
	intStat = stat(pathname, &stFileInfo);
	if (intStat == 0)
		return true;
	return false;
}

bool makeDirectory(const char *pathname, mode_t mode)
{
	string directory = pathname;
	if (directory.empty())
		return true;
	if (directory[directory.size() - 1] != '/')
		directory += '/';
	size_t pos = 1;
	while ((pos = directory.find_first_of('/', pos)) != string::npos)
	{
		string parentDir = directory.substr(0, pos);
		if (!parentDir.empty() && !fileExists(parentDir.c_str()))
		{
			int n = mkdir(parentDir.c_str(), mode);
			if (n) return false;
		}
		pos++;
	}
	return true;
}

const char* getBaseOutputPath(const char *filepath, const char *baseoutpath)
{
	string inpath = filepath;
	size_t fnStart = inpath.find_last_of('/');
	string inname = inpath.substr(fnStart + 1);
	inpath = inpath.substr(0, fnStart + 1);

	size_t extStart = inname.find_last_of('.');
	inname = inname.substr(0, extStart);

	string base;
	if (baseoutpath == NULL || baseoutpath[0] == '\0')
	{
		base = inpath + inname + '/';
	}
	else
	{
		base = baseoutpath;
		if (base[base.size() - 1] != '/')
			base += '/';
		base += inname + '/';
	}
	char *baseStr = (char *) base.c_str();
	size_t baseLen = strlen(baseStr) + 1;
	char *retStr = (char *) malloc(baseLen);
	strncpy(retStr, baseStr, baseLen);
	return retStr;
}

int extract(const char *filepath, const char *baseoutpath)
{
	string basename = getBaseOutputPath(filepath, baseoutpath);

	bool iscomp;			//数据是否压缩
	uint32_t offsetad;		//数据的偏移量
	uint32_t filenum;		//数据个数
	uint32_t filenamelen;		//数据名长度
	int aj = 1;			//aj:解密计数器

	// open input file
	FILE *fin = fopen(filepath, "rb");
	if(!fin) 
	{
		cout << "    Unable to open file!" << endl;
		return 1;
	}

	// check file format
	fseek(fin, -4, SEEK_END);
	unsigned char buf[4];
	fread(&buf[0], sizeof(buf), 1, fin);
	if((buf[0]!=0xef)||(buf[1]!=0x51)||(buf[2]!=0x2a)||(buf[3]!=0x01))
	{
		cout << "    Unrecognzied file format!" << endl;
		return 1;
	}

	// create base output directory
	if (!makeDirectory(basename.c_str(), 0777))
	{
		cout << "    Unable to create directory: " << basename << endl;
		return 1;
	}

	// get offset to header block
	fseek(fin, -8, SEEK_END); //数据头的首偏移量地址在文件的[-8..-5]上存储
	uint32_t filenameblockbegin;
	fread(&filenameblockbegin, sizeof(filenameblockbegin), 1, fin);

	// get number of files
	fseek(fin, filenameblockbegin, SEEK_SET); //指针指向文件头
	fread(&filenum, sizeof(filenum), 1, fin); //文件头的最前四位为数据的个数
	filenameblockbegin = ftell(fin); //保存当前地址

	for(uint32_t i = 0; i < filenum; i++)
	{
		fseek(fin, filenameblockbegin, SEEK_SET);
		fread(&filenamelen, sizeof(filenamelen), 1, fin); //读入文件名大小
		if (filenamelen > 100)
		{
			//测试判断语句,数据名长度不可能超过100
			cout << "    Invalid path name length: " << filenamelen << endl;
			return 1; 
		}

		// get file path
		unsigned char *buff = (unsigned char *)malloc(filenamelen + 1);
		fread(buff, filenamelen, 1, fin); //读取加密的文件名

		// decode filepath
		string origpath;
		for(uint32_t j = 0; j < filenamelen; j++)
		{
			if (aj > 129) aj -= 129;
			uint32_t ii = buff[j] ^ (aj+126); //文件名解密
			origpath += (char) ii;
			aj += 1;
		}
		free(buff);

		// construct output path from original path
		string dataname = basename;
		uint32_t pos = 0;
		// strips windows path (e.g. "E:\0\")
		if (origpath[0] == '\\' && origpath[1] == '\\')
			pos = 2;
		if (origpath[1] == ':' && origpath[2] == '\\')
			pos = 3;
		if (origpath[1] == ':' && origpath[2] != '\\')
		{
			cout << "    Invalid path: " << origpath << endl;
			return 1;
		}
		// convert directory separator '\\' to '/'
		for (uint32_t j = pos; j < origpath.size(); j++)
			dataname += (origpath[j] == '\\' ? '/' : origpath[j]);

		// create directory
		size_t ppos = dataname.find_last_of('/');
		if (ppos != string::npos)
		{
			string datadir = dataname.substr(0, ppos + 1);
			if (!makeDirectory(datadir.c_str(), 0777))
			{
				cout << "    Unable to create directory: " << dataname << endl;
				return 1;
			}
		}

		if (verbose)
		{
			cout << "    " << origpath << " => " << dataname << endl;
		}

		fread(&offsetad, sizeof(offsetad), 1, fin); //下面4字节为数据的绝对偏移量
		filenameblockbegin = ftell(fin); //马上要指向真实数据的绝对偏移量,保存当前指针

		//转移到压缩数据地址绝对偏移量
		fseek(fin, offsetad, SEEK_SET);
		unsigned char sum[8];	//如果数据为压缩数据的话,压缩数据的前八位为压缩文件头
		fread(&sum[0], 8, 1, fin);		//检测是否压缩
		uint32_t bc;
		//通过计算文件头的sum,判断是否压缩
		if((sum[0]+sum[1]+sum[2]+sum[3]+sum[4]+sum[5]+sum[6]+sum[7])!=546)
		{
			iscomp=false;
		}
		else
		{
			iscomp=true;
			fread(&bc, sizeof(bc), 1, fin);	//压缩文件头后4位为压缩前文件的大小
		}
		uint32_t ac; //压缩后或者存储数据的大小
		fseek(fin, filenameblockbegin, SEEK_SET); //回到文件头,读取存储数据的大小
		fread(&ac, sizeof(ac), 1, fin); //读取存储数据的大小
		filenameblockbegin = ftell(fin); //保存文件指针

		// Title
		uint32_t title1len = 0; //第一标题长度
		uint32_t title2len = 0; //第二标题长度

		fseek(fin, 4, SEEK_CUR); //跳过4字节无用信息
		fread(&title1len, sizeof(title1len), 1, fin); //下四字节为标题一长度
		filenameblockbegin = ftell(fin);

		if (title1len > 0) //如果有标题一
		{
			aj += title1len; //增加加密参数
			filenameblockbegin += title1len; //跳过标题一
			fseek(fin, filenameblockbegin, SEEK_SET); //指针跳过标题一
			fread(&title2len, sizeof(title2len), 1, fin); //下四字节为标题二长度
			filenameblockbegin=ftell(fin); //保存指针
		}
		if (title2len > 0) //如果标题二存在
		{
			aj += title2len; //增加加密参数
			filenameblockbegin += title2len; //跳过标题二长度
		}
		fseek(fin, filenameblockbegin, SEEK_SET); //重装指针

		//跳过空字符控制参数
		buf[0] = 0;
		for (unsigned int k = 0; k < 255; k++) // 用于防止死循环
		{
			if(buf[0] > 1) break; //有压缩的格式
			if(buf[0] == 1) filenameblockbegin += 1; //无压缩的格式
			fread(&buf[0], 1, 1, fin);
		}

		if(!iscomp) //测试非压缩数据
		{
			int storep = ftell(fin);
			fseek(fin, offsetad, SEEK_SET);
			char *databuf = (char *) malloc(DATABUF_SIZE);
			FILE *fout = fopen(dataname.c_str(), "wb");
			size_t remaining = ac;
			while (remaining > 0)
			{
				size_t bytes = DATABUF_SIZE > remaining ? remaining : DATABUF_SIZE;
				bytes = fread(&databuf[0], 1, bytes, fin);
				fwrite(&databuf[0], bytes, 1, fout);
				remaining -= bytes;
			}
			fclose(fout);
			free(databuf);
			fseek(fin, storep, SEEK_SET);
		}
		else //测试压缩数据
		{
			int storep = ftell(fin); //要执行解压,保存指针
			fseek(fin, offsetad + 21, SEEK_SET);
			FILE *fout = fopen(dataname.c_str(), "wb");
			lh5Decode(fin, fout, bc, ac);
			fclose(fout);
			fseek(fin, storep, SEEK_SET);
		}
	
		filenameblockbegin = ftell(fin); //保存文件指针
		filenameblockbegin -= 1; //刚读出的字符为有用信息,且已经读出,所以退回
	}
	if (verbose)
	{
		cout << "    Total: " << filenum << " files extracted." << endl;
	}
	fclose(fin);
	return 0;
}

int main(int argc,char *argv[])
{
	cout << "UnWebCompiler for Linux" << endl << endl;

	if (argc == 1)
	{
		// no parameters given. display usage.
		cout << "  Usage: " << argv[0] << " [-d output_base_path] file1.exe file2.exe ..." << endl;
		exit(0);
	}

	// obtain parameters
	vector<string> infiles;
	string outputbasepath;
	int c;
	while ((c = getopt(argc, argv, "vd:")) != -1)
	{
		switch (c)
		{
			case 'v':
				verbose = true;
				break;
			case 'd':
				outputbasepath = optarg;
				printf("%s\n", optarg);
				break;
			default:
				exit(1);
		}
	}

	if (optind >= argc)
	{
		fprintf(stderr, "Expected argument after options.\n");
		exit(EXIT_FAILURE);
	}

	while (optind < argc)
	{
		infiles.insert(infiles.end(), argv[optind++]);
	}

	// process files one by one
	for (unsigned int i = 0; i < infiles.size(); i++)
	{
		cout << "  Extracting " << infiles[i] << endl;
		int n = extract(infiles[i].c_str(), outputbasepath.c_str());
		cout << (!n ? "    ... done" : "    ... failed") << endl;
	}

	exit(EXIT_SUCCESS);
}

