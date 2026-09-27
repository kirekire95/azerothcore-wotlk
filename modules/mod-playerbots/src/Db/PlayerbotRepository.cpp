/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "PlayerbotsDatabase.h"
#include "PlayerbotRepository.h"
#include "AiObjectContext.h"

void PlayerbotRepository::Load(PlayerbotAI* botAI)
{
    ObjectGuid::LowType guid = botAI->GetBot()->GetGUID().GetCounter();

    PlayerbotsDatabasePreparedStatement* stmt = PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_SEL_DB_STORE);
    stmt->SetData(0, guid);
    if (PreparedQueryResult result = PlayerbotsDatabase.Query(stmt))
    {
        std::vector<std::string> values;
        do
        {
            Field* fields = result->Fetch();
            std::string const key = fields[0].Get<std::string>();
            std::string const value = fields[1].Get<std::string>();

            if (key == "value")
                values.push_back(value);
            else if (key == "co")
            {
                botAI->ClearStrategies(BOT_STATE_COMBAT);
                botAI->ChangeStrategy("+chat", BOT_STATE_COMBAT);
                botAI->ChangeStrategy(value, BOT_STATE_COMBAT);
            }
            else if (key == "nc")
            {
                botAI->ClearStrategies(BOT_STATE_NON_COMBAT);
                botAI->ChangeStrategy("+chat", BOT_STATE_NON_COMBAT);
                botAI->ChangeStrategy(value, BOT_STATE_NON_COMBAT);
            }
            else if (key == "dead")
                botAI->ChangeStrategy(value, BOT_STATE_DEAD);
        } while (result->NextRow());

        botAI->GetAiObjectContext()->GetUntypedValue("outfit list");

        botAI->GetAiObjectContext()->Load(values);
    }
}

void PlayerbotRepository::Save(PlayerbotAI* botAI)
{
    ObjectGuid::LowType guid = botAI->GetBot()->GetGUID().GetCounter();

    Reset(botAI);

    PlayerbotsDatabasePreparedStatement* deleteStatement =
        PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_DEL_DB_STORE);
    deleteStatement->SetData(0, guid);
    PlayerbotsDatabase.Execute(deleteStatement);

    std::vector<std::string> data = botAI->GetAiObjectContext()->Save();
    for (std::vector<std::string>::iterator i = data.begin(); i != data.end(); ++i)
    {
        SaveValue(guid, "value", *i);
    }

    SaveValue(guid, "co", FormatStrategies("co", botAI->GetStrategies(BOT_STATE_COMBAT)));
    SaveValue(guid, "nc", FormatStrategies("nc", botAI->GetStrategies(BOT_STATE_NON_COMBAT)));
    SaveValue(guid, "dead", FormatStrategies("dead", botAI->GetStrategies(BOT_STATE_DEAD)));
}

bool PlayerbotRepository::SaveSync(PlayerbotAI* botAI)
{
    ObjectGuid::LowType guid = botAI->GetBot()->GetGUID().GetCounter();
    const std::string combat = FormatStrategies("co", botAI->GetStrategies(BOT_STATE_COMBAT));
    const std::string nonCombat = FormatStrategies("nc", botAI->GetStrategies(BOT_STATE_NON_COMBAT));
    const std::string dead = FormatStrategies("dead", botAI->GetStrategies(BOT_STATE_DEAD));

    PlayerbotsDatabaseTransaction trans = PlayerbotsDatabase.BeginTransaction();

    PlayerbotsDatabasePreparedStatement* stmt =
        PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_DEL_DB_STORE);
    stmt->SetData(0, guid);
    trans->Append(stmt);

    auto appendValue = [&trans, guid](std::string const& key, std::string const& value)
    {
        PlayerbotsDatabasePreparedStatement* valueStmt =
            PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_INS_DB_STORE);
        valueStmt->SetData(0, guid);
        valueStmt->SetData(1, key);
        valueStmt->SetData(2, value);
        trans->Append(valueStmt);
    };

    for (std::string const& value : botAI->GetAiObjectContext()->Save())
        appendValue("value", value);
    appendValue("co", combat);
    appendValue("nc", nonCombat);
    appendValue("dead", dead);

    // The event return checkpoint is deleted from the characters database
    // only after this separate database has durably committed and verified
    // all three exact strategy sets.
    PlayerbotsDatabase.DirectCommitTransaction(trans);

    stmt = PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_SEL_DB_STORE);
    stmt->SetData(0, guid);
    PreparedQueryResult result = PlayerbotsDatabase.Query(stmt);
    if (!result)
        return false;

    bool combatMatched = false;
    bool nonCombatMatched = false;
    bool deadMatched = false;
    do
    {
        Field* fields = result->Fetch();
        std::string const key = fields[0].Get<std::string>();
        std::string const value = fields[1].Get<std::string>();
        if (key == "co")
        {
            if (combatMatched || value != combat)
                return false;
            combatMatched = true;
        }
        else if (key == "nc")
        {
            if (nonCombatMatched || value != nonCombat)
                return false;
            nonCombatMatched = true;
        }
        else if (key == "dead")
        {
            if (deadMatched || value != dead)
                return false;
            deadMatched = true;
        }
    } while (result->NextRow());

    return combatMatched && nonCombatMatched && deadMatched;
}

std::string const PlayerbotRepository::FormatStrategies(std::string const type, std::vector<std::string> strategies){
    std::ostringstream out;
    for (std::vector<std::string>::iterator i = strategies.begin(); i != strategies.end(); ++i)
        out << "+" << (*i).c_str() << ",";

    std::string const res = out.str();
    return res.substr(0, res.size() - 1);
}

void PlayerbotRepository::Reset(PlayerbotAI* botAI)
{
    ObjectGuid::LowType guid = botAI->GetBot()->GetGUID().GetCounter();

    PlayerbotsDatabasePreparedStatement* stmt = PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_DEL_DB_STORE);
    stmt->SetData(0, guid);
    PlayerbotsDatabase.Execute(stmt);
}

void PlayerbotRepository::SaveValue(uint32 guid, std::string const key, std::string const value)
{
    PlayerbotsDatabasePreparedStatement* stmt = PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_INS_DB_STORE);
    stmt->SetData(0, guid);
    stmt->SetData(1, key);
    stmt->SetData(2, value);
    PlayerbotsDatabase.Execute(stmt);
}
