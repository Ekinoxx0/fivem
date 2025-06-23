#pragma once

#include "StdInc.h"

#include <ServerInstanceBase.h>

#include <Client.h>

#include "ComponentExport.h"
#include "ENetPacketUniquePtr.h"
#include "PacketHandler.h"
#include <unordered_set>

class NetGameEventPacketHandlerV2 : public net::PacketHandler<net::packet::ClientNetGameEventV2, HashRageString("msgNetGameEventV2")>
{
	std::unordered_set<uint32_t> blockedEvents;
	std::shared_mutex blockedEventsMutex;
public:

	COMPONENT_EXPORT(CITIZEN_SERVER_IMPL) NetGameEventPacketHandlerV2(fx::ServerInstanceBase* instance);

	static void COMPONENT_EXPORT(CITIZEN_SERVER_IMPL) RouteEvent(const fwRefContainer<fx::ServerGameStatePublic>& sgs, uint32_t bucket, const std::vector<uint16_t>& targetPlayers, const fwRefContainer<fx::ClientRegistry>& clientRegistry, const net::Buffer& data);

	bool COMPONENT_EXPORT(CITIZEN_SERVER_IMPL) Process(fx::ServerInstanceBase* instance, const fx::ClientSharedPtr& client, net::ByteReader& reader, fx::ENetPacketPtr& packet);
	
	static bool COMPONENT_EXPORT(CITIZEN_SERVER_IMPL) ProcessNetEvent(fx::ServerInstanceBase* instance, const fx::ClientSharedPtr& client, net::ByteReader& reader, NetGameEventPacketHandlerV2* handler);
};
