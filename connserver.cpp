#include "connserver.h"

CConnServer ConnManage;

//////////////////////////////////////////

//网络序IP Port
int32 CConnServer::InitConn(uint32 IP,uint16 Port,funcall *lp)
{
	m_Reg = 0;
	m_Sockid = -1;
	m_ServerIP = IP;
	m_ServerPort = Port;
	//发送缓存区
	m_SendBuf.Max = 4096;
	m_SendBuf.Size = 0;
	m_SendBuf.lpBuf = new int8[m_SendBuf.Max];
	if(NULL == m_SendBuf.lpBuf)
		return -1;
	//接收缓存区
	m_RecvBuf.Max = 4096;
	m_RecvBuf.Size = 0;
	m_RecvBuf.lpBuf = new int8[m_RecvBuf.Max];
	if(NULL == m_RecvBuf.lpBuf)
		return -1;

	m_Epoll = epoll_create(1);
	if(m_Epoll <= 0)
		return -1;

	pthread_t th;
	if(0 != pthread_create(&th,NULL,lp,this))
		return -1;

	RegConnServer();
	return 0;
}

int32 CConnServer::AddEpoll()
{
	epoll_event Event;
	Event.data.fd = m_Sockid;
	Event.events   = EPOLLET | EPOLLIN;// | EPOLLOUT

	return epoll_ctl(m_Epoll,EPOLL_CTL_ADD,m_Sockid,&Event);
}

void  CConnServer::DelEpoll()
{
	epoll_event Event;
	epoll_ctl(m_Epoll,EPOLL_CTL_DEL,m_Sockid,&Event);
	close(m_Sockid);
	m_Sockid = -1;//链接已断开
	m_RecvBuf.Size = 0;
	m_SendBuf.Size = 0;
}

int32 CConnServer::ConnServer()
{
	m_Sockid = socket(AF_INET,SOCK_STREAM,0);
	if(m_Sockid <= 0)
		return -1;
	sockaddr_in RemoteAddr;
	socklen_t RemoteAddrLen = sizeof(sockaddr_in);
	RemoteAddr.sin_family = AF_INET;
	RemoteAddr.sin_addr.s_addr = m_ServerIP;
	RemoteAddr.sin_port = m_ServerPort;
	//connect
	if(0 != connect(m_Sockid,(sockaddr*)&RemoteAddr,RemoteAddrLen))
	{
		close(m_Sockid);
		m_Sockid = -1;
		return -1;
	}
	//非阻塞
	if(0 != fcntl(m_Sockid,F_SETFL,O_NONBLOCK | fcntl(m_Sockid,F_GETFL,0)))
	{
		close(m_Sockid);
		m_Sockid = -1;
		return -1;
	}
	//开启keepalive属性
	int32 opt = 1,optlen = sizeof(int32);
	int32 idle = ServerInfo.iHeart,inter = 5,count = 1;
	setsockopt(m_Sockid,SOL_SOCKET,SO_KEEPALIVE,&opt,optlen);
	//设置探测间隔:秒,如该连接在60秒内没有任何数据往来
	setsockopt(m_Sockid,SOL_TCP,TCP_KEEPIDLE,&idle,optlen);
	//设置探测时发包的时间间隔:秒
	setsockopt(m_Sockid,SOL_TCP,TCP_KEEPINTVL,&inter,optlen);
	//设置探测尝试的次数
	setsockopt(m_Sockid,SOL_TCP,TCP_KEEPCNT,&count,optlen);
	//epoll队列
	if(0 != AddEpoll())
	{
		close(m_Sockid);
		m_Sockid = -1;
		return -1;
	}
	return 0;
}

int32 CConnServer::PushRegMsg()
{
	int8 lpBuf[256],*lpTmp = NULL;
	uint16 iLen = SIZE_MSGHEAD;
	MSGHEAD *lpHead = (MSGHEAD*)lpBuf;
	lpHead->MsgReg = htonl(m_Reg);
	lpHead->MsgCmd = htons(CMD_LOGIC_LOGIN);
	lpTmp = lpBuf+SIZE_MSGHEAD;

	//ID	Uint16	2	服务器ID
	*(uint16 *)lpTmp = htons(ServerInfo.ID);
	lpTmp += 2;
	iLen += 2;
	//Condition	Uint16	2	进入条件，游戏币数量
	*(uint16 *)lpTmp = htons(ServerInfo.Condition);
	lpTmp += 2;
	iLen += 2;
	//Type	Uint8	1	类型 1:匹配场 2:自由场 3:比赛场
	*(uint8 *)lpTmp = ServerInfo.Type;
	lpTmp++;
	iLen++;
	//NameLen	Uint8	1	名称长度
	*(uint8 *)lpTmp = ServerInfo.NameLen;
	lpTmp++;
	iLen++;
	//Name	Uint8[64]	64	名称，最长64字节
	if(ServerInfo.NameLen > 0)
	{
		memcpy(lpTmp,ServerInfo.Name,ServerInfo.NameLen);
		lpTmp += ServerInfo.NameLen;
		iLen += ServerInfo.NameLen;
	}
	//Nums	Uint8	1	创建游戏时选择游戏币的个数
	*(uint8 *)lpTmp = ServerInfo.Nums;
	lpTmp++;
	iLen++;
	//Charge	Uint16[5]	2*5	单局对赌的游戏币，最多5个选择
	if(ServerInfo.Nums > 0)
	{
		memcpy(lpTmp,(int8*)ServerInfo.Charge,2*ServerInfo.Nums);
		lpTmp += 2*ServerInfo.Nums;
		iLen += 2*ServerInfo.Nums;
	}
	//PerCharge	Uint8	1	单局收取的场费百分比
	*(uint8*)lpTmp = ServerInfo.iPerCharge;
	lpTmp++;
	iLen++;
	//Order	Uint8	1	排序,值越小位置越靠前
	*(uint8*)lpTmp = ServerInfo.iOrder;
	lpTmp++;
	iLen++;

	lpHead->MsgLen = htons(iLen);
	return PushSend(lpBuf,iLen);
}

int32 CConnServer::PushHeartMsg()
{
	if(m_Sockid <= 0)//链接已断开
		return -1;
	MSGHEAD Head;
	Head.MsgLen = htons(SIZE_MSGHEAD);
	Head.MsgCmd = htons(CMD_HEARTBEAT);
	Head.MsgReg = htonl(m_Reg);
	return PushSend((int8*)&Head,SIZE_MSGHEAD);
}

int32 CConnServer::PushBuffer(BUFFER *lpDst,int8 *lpSrc,int32 Size)
{
	int32 iMax = lpDst->Max;
	int32 iSize = lpDst->Size;
	if(iMax < iSize+Size)
	{
		iMax += iSize+Size;
		int8 *lpTmp = new int8[iMax];
		if(NULL == lpTmp)
		{
			return -1;
		}
		memcpy(lpTmp,lpDst->lpBuf,iSize);
		lpDst->Max = iMax;
		delete [] lpDst->lpBuf;
		lpDst->lpBuf = lpTmp;
	}
	lpDst->Size += Size;
	memcpy(lpDst->lpBuf+iSize,lpSrc,Size);
	return 0;
}

//0:成功 -1:连接断开
int32 CConnServer::SendBuffer()
{
	int32 iSend = 0,iErr = 0;
	MUTEXLOCK Lock(&m_SendLock);
	if(m_SendBuf.Size <= 0)
		return 0;
	while(1)
	{
		iSend = send(m_Sockid,m_SendBuf.lpBuf,m_SendBuf.Size,0);
		if(iSend <= 0)
		{
			iErr = errno;
			//连接断开
			if(0 == iSend || (iErr != EAGAIN && iErr != EINTR))
			{
				//printf("ConnServer send sock:%d closed iErr:%d(EAGAIN:%d EINTR:%d)!\n",m_Sockid,iErr,EAGAIN,EINTR);
				Logger.Log(INFO,"ConnServer closed send sock:%d iErr:%d(EAGAIN:%d EINTR:%d)",m_Sockid,iErr,EAGAIN,EINTR);
				DelEpoll();
				return -1;
			}
			return 0;
		}
		m_SendBuf.Size -= iSend;
		if(m_SendBuf.Size)
			memmove(m_SendBuf.lpBuf,m_SendBuf.lpBuf+iSend,m_SendBuf.Size);
		else
			break;
	}
	return 0;
}

//0:成功 -1:连接断开
int32 CConnServer::RecvBuffer(int8 *lpBuf,int32 Size)
{
	int32 iRecv = 0,iErr = 0;
	while(1)
	{
		iRecv = recv(m_Sockid,lpBuf,Size,0);
		if(iRecv <= 0)
		{
			iErr = errno;
			//连接断开
			if(0 == iRecv || (iErr != EAGAIN && iErr != EINTR))
			{
				//printf("ConnServer recv sock:%d closed iErr:%d(EAGAIN:%d EINTR:%d)!\n",m_Sockid,iErr,EAGAIN,EINTR);
				Logger.Log(INFO,"ConnServer closed recv sock:%d iErr:%d(EAGAIN:%d EINTR:%d)",m_Sockid,iErr,EAGAIN,EINTR);
				DelEpoll();
				return -1;
			}
			return 0;
		}
		PushBuffer(&m_RecvBuf,lpBuf,iRecv);
	}
	return 0;
}

/////////////////////////////////////////////////

