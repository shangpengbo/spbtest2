#include "config.h"

//spb add
//文件夹路径检查 1:已存在 0:创建成功 -1:创建失败
int CheckConfDir(char *lpPath)
{
	int iRet = 1;
	char *lpEnd = NULL,*lpTemp = lpPath;
	if('/' == *lpTemp)
		lpTemp++;

	while((lpEnd = strchr(lpTemp,'/')))
	{
		*lpEnd = 0;

		if(0 != access(lpPath,F_OK))
		{
			iRet = mkdir(lpPath,0755);
		}

		*lpEnd = '/';
		lpTemp = lpEnd+1;
	}
//不检查最后一项
//	if(*lpTemp)
//	{
//		if(0 != access(lpPath,F_OK))
//		{
//			iRet = mkdir(lpPath,0755);
//		}
//	}
	return iRet;
}

//设置文件打开数
int SetNOFile(int iFile)
{
	struct rlimit rlim;
	/* raise open files */
	rlim.rlim_cur = iFile;
	rlim.rlim_max = iFile;
	return setrlimit(RLIMIT_NOFILE, &rlim);
}

/////////////////////////////////////////////

int CUrlConfig::mem_writer(char *data,int size,int nmemb,MEMBUFFER *stream)
{
	int n = size*nmemb;
	int Max = n+stream->Size;
	if(stream->Max < Max)
	{
		Max <<= 1;
		char *lpTmp = new char[Max];
		if(NULL == lpTmp)
		{
			//printf("mem_writer failed new char[%d]\n",stream->Max+1);
			return -1;
		}

		if(stream->lpBuf)
		{
			memcpy(lpTmp,stream->lpBuf,stream->Size);
			delete [] stream->lpBuf;
		}
		stream->Max = Max;
		stream->lpBuf = lpTmp;
	}
	
	memcpy(&stream->lpBuf[stream->Size],data,n);
	stream->Size += n;
	//printf("mem_writer size:%d max:%d n:%d\n",stream->Size,stream->Max,n);
	return n;
}

int CUrlConfig::file_writer(char *data,int size,int nmemb,FILE *stream)
{
	//int n = size*nmemb;
	//printf("file_writer size:%d nmemb:%d n:%d\n",size,nmemb,n);
	return fwrite(data,size,nmemb,stream);
}

int CUrlConfig::mem_init(CURL *&lpConn,const char *lpUrl,MEMBUFFER *stream)
{
	lpConn = curl_easy_init();
	if(NULL == lpConn)
	{
		//printf("curl_easy_init() failed\n");
		return -1;
	}

	CURLcode nCode = curl_easy_setopt(lpConn,CURLOPT_URL,lpUrl);
	if(nCode != CURLE_OK)
	{
		//printf("failed setopt CURLOPT_URL [%d]\n",nCode);
		return -1;
	}

	nCode = curl_easy_setopt(lpConn,CURLOPT_WRITEFUNCTION,mem_writer);
	if(nCode != CURLE_OK)
	{
		//printf("failed setopt CURLOPT_WRITEFUNCTION [%d]\n",nCode);
		return -1;
	}

	nCode = curl_easy_setopt(lpConn,CURLOPT_WRITEDATA,stream);
	if(nCode != CURLE_OK)
	{
		//printf("failed setopt CURLOPT_WRITEDATA [%d]\n",nCode);
		return -1;
	}

	return 0;
}

int CUrlConfig::file_init(CURL *&lpConn,const char *lpUrl,FILE *stream)
{
	lpConn = curl_easy_init();
	if(NULL == lpConn)
	{
		//printf("curl_easy_init() failed\n");
		return -1;
	}

	CURLcode nCode = curl_easy_setopt(lpConn,CURLOPT_URL,lpUrl);
	if(nCode != CURLE_OK)
	{
		//printf("failed setopt CURLOPT_URL [%d]\n",nCode);
		return -1;
	}

	nCode = curl_easy_setopt(lpConn,CURLOPT_WRITEFUNCTION,file_writer);
	if(nCode != CURLE_OK)
	{
		//printf("failed setopt CURLOPT_WRITEFUNCTION [%d]\n",nCode);
		return -1;
	}

	nCode = curl_easy_setopt(lpConn,CURLOPT_WRITEDATA,stream);
	if(nCode != CURLE_OK)
	{
		//printf("failed setopt CURLOPT_WRITEDATA [%d]\n",nCode);
		return -1;
	}

	return 0;
}

int CUrlConfig::file_config(const char *lpUrl,const char* strPath)
{
	m_stream.Size = 0;
	CURL *lpConn = NULL;
	CURLcode nCode = CURLE_OK;
	if(0 != mem_init(lpConn,lpUrl,&m_stream))
	{
		//printf("file_config() mem_init url:%s failed\n",lpUrl);
		curl_easy_cleanup(lpConn);
		return -1;
	}
	nCode = curl_easy_perform(lpConn);
	if(nCode != CURLE_OK)
	{
		//printf("file_config() failed url:%s perform [%d]\n",lpUrl,nCode);
		curl_easy_cleanup(lpConn);
		return -1;
	}
	long iRet = 0;
	nCode = curl_easy_getinfo(lpConn,CURLINFO_RESPONSE_CODE,&iRet);
	if(iRet != 200)
	{
		//printf("file_config() url:%s response failed [%ld]\n",lpUrl,iRet);
		curl_easy_cleanup(lpConn);
		return -1;
	}
	curl_easy_cleanup(lpConn);

	if(0 == m_stream.Size)
	{
		return 1;
	}
	CheckConfDir((char*)strPath);
	FILE *lpFile = fopen(strPath,"wb");
	if(NULL == lpFile)
	{
		return -2;
	}
	fwrite(m_stream.lpBuf,1,m_stream.Size,lpFile);
	fclose(lpFile);

	return 0;
}

/////////////////////////////////////////////


//解析配置,通知状态变更
int32 ReadConfig(int32 &iCode,int32 &iReCnf,int8 *lpPath)
{
	ifstream ifs;
	ifs.open(lpPath);

	Json::Reader reader;
	Json::Value root;
	if(!reader.parse(ifs,root))
	{
		printf("jsoncpp failed parse config\n");
		ifs.close();
		return -1;
	}
	ifs.close();

	iCode = root["CONFIG"]["CODE"].asInt();
	iReCnf = root["CONFIG"]["RECNF"].asInt();
	int32 iTmp = 0;
	iTmp = ServerInfo.iFile;
	ServerInfo.iFile = root["CONFIG"]["NOFILE"].asInt();
	if(iTmp != ServerInfo.iFile)
		SetNOFile(ServerInfo.iFile);

	iTmp = ServerInfo.iStatus;
	ServerInfo.iStatus = root["CONFIG"]["STATUS"].asInt();
	if(iTmp != ServerInfo.iStatus)
	{
		//通知状态变更
	}

	return 0;
}

//解析配置,初始程序
int32 InitServer(int32 &iCode,int32 &iReCnf,int8 *lpPath)
{
	ifstream ifs;
	ifs.open(lpPath);

	Json::Reader reader;
	Json::Value root;
	if(!reader.parse(ifs,root))
	{
		printf("jsoncpp failed parse config\n");
		ifs.close();
		return -1;
	}
	ifs.close();

	iCode = root["CONFIG"]["CODE"].asInt();
	iReCnf = root["CONFIG"]["RECNF"].asInt();

	//全局变量
	ServerInfo.iStatus = root["CONFIG"]["STATUS"].asInt();
	ServerInfo.iMaxRoomUser = root["SERVER"]["MAXROOMUSER"].asInt();
        ServerInfo.iMinRoomUser = root["SERVER"]["MINROOMUSER"].asInt();
	string sIP = root["SERVER"]["IP"].asString();
	ServerInfo.iServerIP = inet_addr(sIP.c_str());
	ServerInfo.iHeart = root["SERVER"]["HEART"].asUInt();
	ServerInfo.iMsgUnit = root["MEMPOOL"]["MSGUNIT"].asUInt();
	ServerInfo.iRoomCharge = root["SERVER"]["ROOMCHARGE"].asInt();

	//当前服务器信息
	ServerInfo.Type = root["SERVER"]["TYPE"].asInt();
	ServerInfo.ID = root["SERVER"]["ID"].asInt();
	string sName = root["SERVER"]["NAME"].asString();
	ServerInfo.NameLen = sName.size();
	if(ServerInfo.NameLen > MAX_NAME)
		ServerInfo.NameLen = MAX_NAME;
	memcpy(ServerInfo.Name,sName.c_str(),ServerInfo.NameLen);
	ServerInfo.Condition = root["SERVER"]["CONDITION"].asInt();
	ServerInfo.Nums = root["SERVER"]["CHARGE"].size();
	if(ServerInfo.Nums > MAX_CHARGE)
		ServerInfo.Nums = MAX_CHARGE;
	for(uint8 i = 0; i < ServerInfo.Nums;i++)
	{
		ServerInfo.Charge[i] = root["SERVER"]["CHARGE"][i].asInt();
	}
	ServerInfo.iPerCharge = root["SERVER"]["PERCHARGE"].asInt();
	ServerInfo.iExpWin = root["SERVER"]["EXPWIN"].asInt();
	ServerInfo.iExpLose = root["SERVER"]["EXPLOSE"].asInt();
	ServerInfo.iExpEscape = root["SERVER"]["EXPESCAPE"].asInt();
	ServerInfo.iOrder = root["SERVER"]["ORDER"].asInt();

	ServerInfo.iFile = root["CONFIG"]["NOFILE"].asInt();
	//文件打开数
	SetNOFile(ServerInfo.iFile);

	int32 iOpen = root["LOG"]["OPEN"].asInt();
	string sPath = root["LOG"]["PATH"].asString();
	int32 iLevel = root["LOG"]["LEVEL"].asInt();
	//初始日志文件
	if(false == Logger.Init(iOpen,(char*)sPath.c_str(),0,0,ServerInfo.ID,0,0,iLevel))
	{
		printf("Logger init failed!\n");
		return -1;
	}

	uint32 iUserInit = root["MEMPOOL"]["USERINIT"].asUInt();
	uint32 iUserGrow = root["MEMPOOL"]["USERGROW"].asUInt();
	//用户管理
	if(0 != UserManage.InitManage(iUserInit,iUserGrow))
	{
		printf("UserManage init failed!\n");
		return -1;
	}

	//socket服务程序
	if(0 != UDPServer.InitServer())
	{
		printf("UDPServer init failed!\n");
		return -1;
	}

    	//socket服务程序
	if(0 != UDPSendServer.InitServer())
	{
		printf("UDPServer init failed!\n");
		return -1;
	}

	uint16 iPort1 = root["SERVER"]["PORTSTART"].asInt();
	uint16 iPort2 = root["SERVER"]["PORTEND"].asInt();
	//房间管理
	if(0 != RoomManage.InitManage(iPort1,iPort2))
	{
		printf("RoomManage init failed!\n");
		return -1;
	}


	int32 iThread = root["THREAD"]["CLIENT"].asInt();
	pthread_t th;
	//初始client业务处理线程
	for(int32 i = 0;i < iThread;i++)
	{
		if(0 != pthread_create(&th,NULL,ClientBusiness,&UDPServer))
		{
			printf("client thread create failed!\n");
			return -1;
		}
	}

    	for(int32 i = 0;i < iThread;i++)
	{
		if(0 != pthread_create(&th,NULL,SendUdpData,&UDPSendServer))
		{
			printf("client thread create failed!\n");
			return -1;
		}
	}

	if(0 != pthread_create(&th,NULL,CheckFreeRoom,&UDPSendServer))
	{
		printf("client CheckFreeRoom create failed!\n");
		return -1;
	}

	sIP = root["MANAGER"]["IP"].asString();
	uint16 iPort = root["MANAGER"]["PORT"].asInt();
	//初始连接manage服务程序
	if(0 != ConnManage.InitConn(inet_addr(sIP.c_str()),htons(iPort),ManageBusiness))
	{
		printf("ConnManage init failed!\n");
		return -1;
	}

	return 0;
}

