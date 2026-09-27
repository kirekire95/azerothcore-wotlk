/*
 * mod-gtapolice
 * GM chat commands for testing and control
 */

#ifndef GTA_POLICE_COMMANDS_H
#define GTA_POLICE_COMMANDS_H

#include "ScriptMgr.h"
#include "Chat.h"
#include "ChatCommand.h"

using namespace Acore::ChatCommands;

class gtapolice_commands : public CommandScript
{
public:
    gtapolice_commands();
    ChatCommandTable GetCommands() const override;

private:
    static bool HandleWantedCommand(ChatHandler* handler, Optional<PlayerIdentifier> target, std::string action);
    static bool HandlePursueCommand(ChatHandler* handler, Optional<PlayerIdentifier> target);
    static bool HandleArrestCommand(ChatHandler* handler, Optional<PlayerIdentifier> target);
    static bool HandleStatusCommand(ChatHandler* handler);
    static bool HandleClearCommand(ChatHandler* handler, Optional<PlayerIdentifier> target);
    static bool HandleBribeCommand(ChatHandler* handler, Optional<PlayerIdentifier> target);
    static bool HandleHeatCommand(ChatHandler* handler, Optional<uint32> zoneId);
    static bool HandleReloadCommand(ChatHandler* handler);
};

#endif // GTA_POLICE_COMMANDS_H
