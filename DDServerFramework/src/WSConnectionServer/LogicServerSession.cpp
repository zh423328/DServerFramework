#include <unordered_map>
#include <set>
using namespace std;

#include "ConnectionServerSendOP.h"
#include "ConnectionServerRecvOP.h"
#include "ClientSession.h"
#include "WrapLog.h"
#include "packet.h"

#include "../../ServerConfig/ServerConfig.pb.h"
#include "./proto/LogicServerWithConnectionServer.pb.h"

#include "ClientSessionMgr.h"
#include "LogicServerSessionMgr.h"

#include "LogicServerSession.h"

extern WrapServer::PTR                      gServer;
extern WrapLog::PTR                         gDailyLogger;
extern ServerConfig::ConnectionServerConfig connectionServerConfig;

enum class CELLNET_OP:uint32_t
{
    LOGICSERVER_LOGIN = 3280140517,
    LOGICSERVER_DOWNSTREAM = 1648565662,
    LOGICSERVER_KICK_PLAYER = 2806502720,
    LOGICSERVER_SET_PLAYER_SLAVE = 1999458104,
};

LogicServerSession::LogicServerSession()
{
    mIsPrimary = false;
    mID = -1;
    mSendSerialID = 1;
}

void LogicServerSession::sendPBData(uint32_t cmd, const char* data, size_t len)
{
    auto tmp = std::make_shared<std::string>(data, len);

    getEventLoop()->pushAsyncProc([=](){
        /*  ���л�cellnet packet   */
        char b[8 * 1024];
        Packet packet(b, sizeof(b), true);
        packet.writeUINT32(cmd, false);
        packet.writeUINT16(mSendSerialID, false);
        packet.writeUINT16(tmp->size() + 8, false);
        packet.writeBuffer(tmp->c_str(), tmp->size());

        sendPacket(packet.getData(), packet.getPos());

        mSendSerialID++;
    });
}

void LogicServerSession::onEnter()
{
    gDailyLogger->warn("recv logic server enter");
}

void LogicServerSession::onClose()
{
    gDailyLogger->warn("recv logic server dis connect, server id : {}.", mID);

    if (mID != -1)
    {
        if (mIsPrimary)
        {
            ClientSessionMgr::KickClientOfPrimary(mID);
            LogicServerSessionMgr::RemovePrimaryLogicServer(mID);
        }
        else
        {
            LogicServerSessionMgr::RemoveSlaveLogicServer(mID);
        }

        mIsPrimary = false;
        mID = -1;
    }
}

bool LogicServerSession::checkPassword(const std::string& password)
{
    return password == connectionServerConfig.logicserverloginpassword();
}

void LogicServerSession::sendLogicServerLoginResult(bool isSuccess, const std::string& reason)
{
    internalAgreement::LogicServerLoginReply loginResult;
    loginResult.set_issuccess(isSuccess);
    loginResult.set_id(mID);

    sendPB(966232901, loginResult);
}

void LogicServerSession::procPacket(uint32_t op, const char* body, PACKET_LEN_TYPE bodyLen)
{
    gDailyLogger->debug("recv logic server packet, op:{}, bodylen:{}", op, bodyLen);

    ReadPacket rp(body, bodyLen);
    switch (static_cast<CELLNET_OP>(op))
    {
    case CELLNET_OP::LOGICSERVER_LOGIN:
        {
            onLogicServerLogin(rp);
        }
        break;
    case CELLNET_OP::LOGICSERVER_DOWNSTREAM:
        {
            onPacket2ClientByRuntimeID(rp);
        }
        break;
    case CELLNET_OP::LOGICSERVER_KICK_PLAYER:
        {
            onKickClientByRuntimeID(rp);
        }
        break;
    case CELLNET_OP::LOGICSERVER_SET_PLAYER_SLAVE:
        {
            onSlaveServerIsSetClient(rp);
        }
        break;
        default:
        {
            assert(false);
        }
        break;
    }
}

void LogicServerSession::onLogicServerLogin(ReadPacket& rp)
{
    internalAgreement::LogicServerLogin loginMsg;
    if (loginMsg.ParseFromArray(rp.getBuffer(), rp.getMaxPos()))
    {
        gDailyLogger->info("�յ��߼���������½, ID:{},����:{}", loginMsg.id(), "");

        bool loginResult = false;
        string reason;

        if (true || checkPassword(""))
        {
            bool isSuccess = false;

            if (loginMsg.isprimary())
            {
                if (LogicServerSessionMgr::FindPrimaryLogicServer(loginMsg.id()) == nullptr)
                {
                    LogicServerSessionMgr::AddPrimaryLogicServer(loginMsg.id(), std::static_pointer_cast<LogicServerSession>(shared_from_this()));
                    isSuccess = true;
                }
            }
            else
            {
                if (LogicServerSessionMgr::FindSlaveLogicServer(loginMsg.id()) == nullptr)
                {
                    LogicServerSessionMgr::AddSlaveLogicServer(loginMsg.id(), std::static_pointer_cast<LogicServerSession>(shared_from_this()));
                    isSuccess = true;
                }
            }

            if (isSuccess)
            {
                mIsPrimary = loginMsg.isprimary();
                loginResult = true;
                mID = loginMsg.id();
            }
            else
            {
                reason = "ID��Ӧ��Logic Server�Ѵ���";
            }
        }
        else
        {
            reason = "�������";
        }

        sendLogicServerLoginResult(loginResult, reason);
    }
}

const static bool IsPrintPacketSendedLog = true;

void LogicServerSession::onPacket2ClientByRuntimeID(ReadPacket& rp)
{
    internalAgreement::DownstreamACK downstream;
    if (downstream.ParseFromArray(rp.getBuffer(), rp.getMaxPos()))
    {
        for (auto& v : downstream.clientid())
        {
            ConnectionClientSession::PTR client = ClientSessionMgr::FindClientByRuntimeID(v);
            if (client != nullptr)
            {
                client->sendPBBinary(downstream.msgid(), downstream.data().c_str(), downstream.data().size());
            }
        }
    }
}

void LogicServerSession::onSlaveServerIsSetClient(ReadPacket& rp)
{
    internalAgreement::LogicServerSetRoleSlave setslaveMsg;
    if (setslaveMsg.ParseFromArray(rp.getBuffer(), rp.getMaxPos()))
    {
        ConnectionClientSession::PTR p = ClientSessionMgr::FindClientByRuntimeID(setslaveMsg.roleruntimeid());
        if (p != nullptr)
        {
            p->setSlaveServerID(setslaveMsg.isset() ? mID : -1);
        }
    }
}

void LogicServerSession::onKickClientByRuntimeID(ReadPacket& rp)
{
    internalAgreement::LogicServerKickPlayer kickMsg;
    if (kickMsg.ParseFromArray(rp.getBuffer(), rp.getMaxPos()))
    {
        ClientSessionMgr::KickClientByRuntimeID(kickMsg.roleruntimeid());
    }
}
