#ifndef __PLUGIN_COMMS_TWO_SIX_CORE_H_
#define __PLUGIN_COMMS_TWO_SIX_CORE_H_

#include <map>
#include <sstream>
#include <thread>

#include "IRacePluginComms.h"
#include <jel/jel.h>

class PluginCommsTwoSixCore : public IRacePluginComms {
private:
    IRaceSdkComms *raceSdk;
    std::string racePersona;
    std::map<LinkID, std::string> linkProfiles;
    std::map<LinkID, std::vector<std::string>> linkPersonas;
    std::map<ConnectionID, LinkType> connectionLinkTypes;
    std::map<ConnectionID, LinkID> connectionLinkIds;
    std::map<ConnectionID, std::thread> connectionThreads;
    std::map<ConnectionID, int> connectionSockets;
    int connectionCounter;
    jel_config *jel_cfg;
    int unwedge(int sock, void **pMessage, size_t *msgLen);
    void directConnectionMonitor(ConnectionID connectionId, std::string hostname, int port);
    void broadcastConnectionMonitor(ConnectionID connectionId, std::string hostname, int port,
                                    int checkFrequency);

public:
    PluginCommsTwoSixCore();
    void init(IRaceSdkComms *sdk, const std::string &globalConfigFilePath,
              const std::string &pluginConfigFilePath) override;
    void start() override;
    virtual void shutdown() override;
    virtual LinkProperties getLinkProperties(LinkType linkType, LinkID linkId) override;
    virtual LinkProperties getConnectionProperties(LinkType linkType,
                                                   ConnectionID connectionId = "") override;
    void sendPackage(ConnectionID connectionId, EncPkg pkg) override;
    void sendPackageDirectLink(EncPkg pkg, std::string hostname, int port);
    void sendPackageBroadcast(EncPkg pkg, std::string hostname, int port);
    EncPkg receivePackage(ConnectionID connectionId);
    ConnectionID openConnection(LinkType linkType, LinkID linkId, std::string config) override;
    void closeConnection(ConnectionID connectionId) override;

protected:
    void log(const std::string &message);
};

#endif
