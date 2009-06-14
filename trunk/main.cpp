#include<iostream>
#include<fstream>
#include<vector>
#include<iomanip>
#include<cstring>
#include<cstdlib>
using namespace std;

#include<sys/stat.h>
#include<sys/types.h>

int main(int argc,char *argv[])
{
/*
	for (int i=0;i<argc;i++)
	cout<<argv[i]<<endl;
*/
	if(argc>2)		//只解压单个文件
	{
		cout<<"请解压单个文件!"<<endl;
		return 1;
	}
	char *in=argv[1];	//要解压的文件

	string ts=in,basename;
	basename.assign(ts,0,ts.size()-4);//文件名截除后缀

	bool iscomp;			//数据是否压缩
	long offsetad;			//数据的偏移量
	long filenum;			//数据个数
	long filenamelen;		//数据名长度
	int aj=1,ii;		//aj:解密计数器
	int k=0;			//跳过空字符控制参数
	long title1len;		//第一标题长度
	long title2len;		//第二标题长度
	string dataname,cls;

	char maindir[30];
	strcpy(maindir,basename.c_str());
	mkdir(maindir,0777);		//创建主文件夹

	fstream fin(in,ios_base::in|ios_base::binary);
	if(!fin) 
	{
		cout<<"文件不存在!"<<endl;
		return 1;
	}

	fin.seekg(-4,ios_base::end);
	vector<char> buf(4);
	fin.read(&buf[0],4);//简单文件判断,看是否为正确的文件
	if(((int)buf[0]!=-17)||((int)buf[1]!=81)||((int)buf[2]!=42)||((int)buf[3]!=1))
	{
		cout<<"不是可解压文件!"<<endl;
		return 1;
	}

	fin.seekg(-8,ios_base::end);//数据头的首偏移量地址在文件的[-8..-5]上存储
	long filenameblockbegin;
	fin.read((char*)&filenameblockbegin,sizeof(filenameblockbegin));
	//cout<<"文件数据块的起始偏移地址:"<<filenameblockbegin<<endl;		//测试语句,文件头数据块的偏移地址,对照软件,看是否出错

	fin.seekg(filenameblockbegin,ios_base::beg);//指针指向文件头
	fin.read((char*)&filenum,sizeof(filenum));//文件头的最前四位为数据的个数
//	cout<<"数据个数:"<<filenum<<endl;		//数据个数
	filenameblockbegin=fin.tellg();			//保存当前地址

	//for(int i=0;i<filenum;i++)
	for(int i=0;i<filenum;i++)
	{
		//cout<<">>>>>>>>>>第 "<<i+1<<" 个文件<<<<<<<<<<<<<<<<<<<<<<<<<<<"<<endl;
		fin.seekg(filenameblockbegin,ios_base::beg);
		fin.read((char*)&filenamelen,sizeof(filenamelen));//读入文件名大小
		//cout<<"数据名大小:"<<filenamelen-5<<endl;
		if(filenamelen>100) return 1;			//测试判断语句,数据名长度不可能超过100

		vector<char> buff(filenamelen);
		fin.read(&buff[0],filenamelen);			//读取加密的文件名
		dataname+='.';
		dataname+='/';
		dataname+=basename;
		dataname+='/';			//创建文件的相对路径:"./basename/....."
		for(int j=0;j<filenamelen;j++)
		{
			if(aj>129) aj-=129;
			ii=buff[j]^(aj+126);			//文件名解密
			if(j>4) 					//删除文件中使用的windows路径:"E:\0\"
			{
				char t1=(char)ii;
				if(t1==92)		//把windows下的目录符号'\'改为linux下的目录符号'/'
				{
					char subdir[50];
					strcpy(subdir,dataname.c_str());
					mkdir (subdir,0777);
					t1=47;
				}
				dataname+=t1;
			}
			aj+=1;
		}
		//cout<<"文件名:"<<dataname<<endl;


		fin.read((char*)&offsetad,sizeof(offsetad));//下面4字节为数据的绝对偏移量
	//	cout<<"偏移量:"<<offsetad<<endl;


		filenameblockbegin=fin.tellg();		//马上要指向真实数据的绝对偏移量,保存当前指针

		//转移到压缩数据地址绝对偏移量
		fin.seekg(offsetad,ios_base::beg);
		vector <char> sum(8);	//如果数据为压缩数据的话,压缩数据的前八位为压缩文件头
		fin.read(&sum[0],8);		//检测是否压缩
		if((sum[0]+sum[1]+sum[2]+sum[3]+sum[4]+sum[5]+sum[6]+sum[7])!=546)//如果不是压缩数据
		{					//通过计算文件头的sum,判断是否压缩
			iscomp=false;
		//	cout<<"----------------------Not Compressed"<<endl;
		}
		else//如果是压缩数据
		{
			iscomp=true;
			long bc;
			fin.read((char*)&bc,sizeof(bc));	//压缩文件头后4位为压缩前文件的大小
		//	cout<<"压缩前大小:"<<bc<<endl;
		}
		long ac;	//压缩后或者存储数据的大小
		fin.seekg(filenameblockbegin,ios_base::beg);	//回到文件头,读取存储数据的大小
		fin.read((char*)&ac,sizeof(ac));			//读取存储数据的大小
		filenameblockbegin=fin.tellg();			//保存文件指针
		/*if(iscomp)
			cout<<"压缩数据大小:";
		else
			cout<<"存储数据大小:";
		cout<<ac<<endl;
		*/

		fin.seekg(4,ios_base::cur);//跳过4字节无用信息
		fin.read((char*)&title1len,sizeof(title1len));//下四字节为标题一长度
		filenameblockbegin=fin.tellg();

		if(title1len>0)		//如果有标题一
		{
		//	cout<<"title1len: "<<title1len<<endl;
			aj+=title1len;		//增加加密参数
			filenameblockbegin+=title1len;//跳过标题一
			fin.seekg(filenameblockbegin,ios_base::beg);//指针跳过标题一
			fin.read((char*)&title2len,sizeof(title2len));	//下四字节为标题二长度
			filenameblockbegin=fin.tellg();			//保存指针

		}
		if(title2len>0)		//如果标题二存在
		{
			//cout<<"title2len:"<<title2len<<endl;
			aj+=title2len;		//增加加密参数
			filenameblockbegin+=title2len;	//跳过标题二长度
		}
		fin.seekg(filenameblockbegin,ios_base::beg);	//重装指针

		buf[0]=0;
		for(k=0;k!=255;k++)// 用于防止死循环
		{
		if((int)buf[0]>1) {break;};	//有压缩的格式
		if((int)buf[0]==1) filenameblockbegin+=1;	//无压缩的格式
		fin.read(&buf[0],1);
		}

		if(!iscomp)	//测试非压缩数据
		{
			//cout<<"Uncompress"<<endl;
			int storep=fin.tellg();
			fin.seekg(offsetad,ios_base::beg);
			vector<char> out(ac);
			fin.read(&out[0],ac);
			char filename[50];
			strcpy(filename,dataname.c_str());
			fstream of(filename,ios_base::out|ios::binary);
			of.write(&out[0],ac);
			of.close();
			fin.seekg(storep,ios_base::beg);
		}

		else//测试压缩数据
		{
			int storep=fin.tellg();		//要执行解压,保存指针
			fin.seekg(offsetad,ios_base::beg);
			vector<char>out(ac);		//ac为存储数据的大小
			fin.read(&out[0],ac);
			vector<char>com(ac);
			for(int j1=8;j1<12;j1++)
				com[j1-8]=out[j1];
			for(int j1=4;j1<ac-16;j1++)
				com[j1]=out[j1+17];
			fstream of("temp.temp",ios_base::out|ios::binary);
			of.write(&com[0],ac-17);
			of.close();
			string cmd;
			cmd+="lh5 temp.temp ";
			cmd+=dataname;
			char filename[50];
			strcpy(filename,cmd.c_str());

			system(filename);
			system("rm temp.temp");
			fin.seekg(storep,ios_base::beg);
		}
	
		dataname=cls;//	文件名清空用于下一次循环
		filenameblockbegin=fin.tellg();	//保存文件指针
		filenameblockbegin-=1;//刚读出的字符为有用信息,且已经读出,所以退回

	}
	cout<<" 共有 "<<filenum<<" 个文件被解压!"<<endl;
	fin.close();
//	fout.close();
}
