#!/bin/bash
#
# Alive World Module - Database Status Viewer
# Shows bot memory, gossip, zone activity, and AI cache status
#
# Usage: ./alive_status.sh [section]
#   sections: all, bots, zones, gossip, cache, connections
#

# Database credentials (same as AzerothCore)
DB_HOST="${DB_HOST:-localhost}"
DB_PORT="${DB_PORT:-3306}"
DB_USER="${DB_USER:-acore}"
DB_PASS="${DB_PASS:-acore}"
DB_CHARS="acore_characters"
DB_WORLD="acore_world"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# MySQL command helper
mysql_cmd() {
    mysql -h "$DB_HOST" -P "$DB_PORT" -u "$DB_USER" -p"$DB_PASS" --batch --skip-column-names "$@" 2>/dev/null
}

mysql_table() {
    mysql -h "$DB_HOST" -P "$DB_PORT" -u "$DB_USER" -p"$DB_PASS" --table "$@" 2>/dev/null
}

section="${1:-all}"

echo -e "${CYAN}╔════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}║           ALIVE WORLD MODULE - DATABASE STATUS                 ║${NC}"
echo -e "${CYAN}╚════════════════════════════════════════════════════════════════╝${NC}"
echo ""

# ============================================================================
# BOT MEMORY STATS
# ============================================================================
if [[ "$section" == "all" || "$section" == "bots" ]]; then
    echo -e "${GREEN}┌──────────────────────────────────────────────────────────────────┐${NC}"
    echo -e "${GREEN}│ BOT MEMORY                                                       │${NC}"
    echo -e "${GREEN}└──────────────────────────────────────────────────────────────────┘${NC}"

    # Total bots with memory
    total_bots=$(mysql_cmd -e "SELECT COUNT(*) FROM alive_bot_memory" "$DB_CHARS")
    echo -e "Total bots with memory: ${YELLOW}${total_bots:-0}${NC}"

    # Personality distribution
    echo -e "\n${BLUE}Personality Distribution:${NC}"
    mysql_table -e "
        SELECT
            CASE
                WHEN aggression > 50 THEN 'Aggressive'
                WHEN aggression < -50 THEN 'Passive'
                WHEN caution > 50 THEN 'Cautious'
                WHEN sociability > 50 THEN 'Social'
                ELSE 'Balanced'
            END as personality_type,
            COUNT(*) as count
        FROM alive_bot_memory
        GROUP BY personality_type
        ORDER BY count DESC
    " "$DB_CHARS" 2>/dev/null || echo "  (No data)"

    # Top killers
    echo -e "\n${BLUE}Top 5 Bot Killers:${NC}"
    mysql_table -e "
        SELECT bot_name, total_kills, total_deaths,
               ROUND(total_kills / GREATEST(total_deaths, 1), 2) as kd_ratio
        FROM alive_bot_memory
        ORDER BY total_kills DESC
        LIMIT 5
    " "$DB_CHARS" 2>/dev/null || echo "  (No data)"

    # Recent activity
    echo -e "\n${BLUE}Recently Active Bots (last hour):${NC}"
    mysql_table -e "
        SELECT bot_guid, bot_name, last_zone_id, last_activity_time
        FROM alive_bot_memory
        WHERE last_activity_time > DATE_SUB(NOW(), INTERVAL 1 HOUR)
        ORDER BY last_activity_time DESC
        LIMIT 10
    " "$DB_CHARS" 2>/dev/null || echo "  (No data)"
    echo ""
fi

# ============================================================================
# SOCIAL CONNECTIONS
# ============================================================================
if [[ "$section" == "all" || "$section" == "connections" ]]; then
    echo -e "${GREEN}┌──────────────────────────────────────────────────────────────────┐${NC}"
    echo -e "${GREEN}│ SOCIAL CONNECTIONS                                               │${NC}"
    echo -e "${GREEN}└──────────────────────────────────────────────────────────────────┘${NC}"

    # Connection stats
    total_connections=$(mysql_cmd -e "SELECT COUNT(*) FROM alive_bot_social_connections" "$DB_CHARS")
    nemesis_count=$(mysql_cmd -e "SELECT COUNT(*) FROM alive_bot_social_connections WHERE is_nemesis = 1" "$DB_CHARS")
    friend_count=$(mysql_cmd -e "SELECT COUNT(*) FROM alive_bot_social_connections WHERE is_friend = 1" "$DB_CHARS")

    echo -e "Total connections: ${YELLOW}${total_connections:-0}${NC}"
    echo -e "Nemesis pairs: ${RED}${nemesis_count:-0}${NC}"
    echo -e "Friend pairs: ${GREEN}${friend_count:-0}${NC}"

    # Top nemeses
    echo -e "\n${BLUE}Top Nemesis Relationships:${NC}"
    mysql_table -e "
        SELECT b.bot_name, c.target_name, c.kills_vs_target, c.deaths_to_target, c.sentiment
        FROM alive_bot_social_connections c
        JOIN alive_bot_memory b ON c.bot_guid = b.bot_guid
        WHERE c.is_nemesis = 1
        ORDER BY c.harmed_count DESC
        LIMIT 5
    " "$DB_CHARS" 2>/dev/null || echo "  (No data)"
    echo ""
fi

# ============================================================================
# COMBAT LOG
# ============================================================================
if [[ "$section" == "all" || "$section" == "combat" ]]; then
    echo -e "${GREEN}┌──────────────────────────────────────────────────────────────────┐${NC}"
    echo -e "${GREEN}│ RECENT COMBAT                                                    │${NC}"
    echo -e "${GREEN}└──────────────────────────────────────────────────────────────────┘${NC}"

    total_encounters=$(mysql_cmd -e "SELECT COUNT(*) FROM alive_bot_combat_log" "$DB_CHARS")
    echo -e "Total recorded encounters: ${YELLOW}${total_encounters:-0}${NC}"

    # Recent combat
    echo -e "\n${BLUE}Last 10 Combat Encounters:${NC}"
    mysql_table -e "
        SELECT
            b.bot_name,
            c.enemy_name,
            c.enemy_level,
            CASE c.outcome
                WHEN 0 THEN 'Fled'
                WHEN 1 THEN 'Won'
                WHEN 2 THEN 'Lost'
                ELSE 'Draw'
            END as outcome,
            c.damage_dealt,
            c.damage_taken,
            c.encounter_time
        FROM alive_bot_combat_log c
        JOIN alive_bot_memory b ON c.bot_guid = b.bot_guid
        ORDER BY c.encounter_time DESC
        LIMIT 10
    " "$DB_CHARS" 2>/dev/null || echo "  (No data)"
    echo ""
fi

# ============================================================================
# ZONE ACTIVITY
# ============================================================================
if [[ "$section" == "all" || "$section" == "zones" ]]; then
    echo -e "${GREEN}┌──────────────────────────────────────────────────────────────────┐${NC}"
    echo -e "${GREEN}│ ZONE ACTIVITY HISTORY                                            │${NC}"
    echo -e "${GREEN}└──────────────────────────────────────────────────────────────────┘${NC}"

    # Zone config count
    zone_count=$(mysql_cmd -e "SELECT COUNT(*) FROM alive_zone_config" "$DB_WORLD")
    echo -e "Configured zones: ${YELLOW}${zone_count:-0}${NC}"

    # High danger zones
    echo -e "\n${BLUE}High Danger Zones (base_danger > 0.7):${NC}"
    mysql_table -e "
        SELECT zone_id, zone_name,
               CASE controlling_faction
                   WHEN 0 THEN 'Neutral'
                   WHEN 2 THEN 'Alliance'
                   WHEN 4 THEN 'Horde'
               END as faction,
               base_danger_level,
               pvp_hotspot
        FROM alive_zone_config
        WHERE base_danger_level > 0.7
        ORDER BY base_danger_level DESC
        LIMIT 10
    " "$DB_WORLD" 2>/dev/null || echo "  (No data)"

    # Recent activity snapshots
    echo -e "\n${BLUE}Recent Zone Snapshots:${NC}"
    mysql_table -e "
        SELECT zone_id, player_count, bot_count, danger_level,
               combat_events, death_count, snapshot_time
        FROM alive_zone_activity_history
        ORDER BY snapshot_time DESC
        LIMIT 10
    " "$DB_CHARS" 2>/dev/null || echo "  (No data)"
    echo ""
fi

# ============================================================================
# GOSSIP
# ============================================================================
if [[ "$section" == "all" || "$section" == "gossip" ]]; then
    echo -e "${GREEN}┌──────────────────────────────────────────────────────────────────┐${NC}"
    echo -e "${GREEN}│ GOSSIP POOL                                                      │${NC}"
    echo -e "${GREEN}└──────────────────────────────────────────────────────────────────┘${NC}"

    # Active gossip
    active_gossip=$(mysql_cmd -e "SELECT COUNT(*) FROM alive_world_gossip WHERE expires_at > NOW()" "$DB_CHARS")
    expired_gossip=$(mysql_cmd -e "SELECT COUNT(*) FROM alive_world_gossip WHERE expires_at <= NOW()" "$DB_CHARS")

    echo -e "Active gossip items: ${YELLOW}${active_gossip:-0}${NC}"
    echo -e "Expired (pending cleanup): ${RED}${expired_gossip:-0}${NC}"

    # Recent gossip
    echo -e "\n${BLUE}Recent Gossip:${NC}"
    mysql_table -e "
        SELECT gossip_type, subject_name, zone_id, importance, spread_count, origin_time
        FROM alive_world_gossip
        WHERE expires_at > NOW()
        ORDER BY origin_time DESC
        LIMIT 10
    " "$DB_CHARS" 2>/dev/null || echo "  (No data)"
    echo ""
fi

# ============================================================================
# AI CACHE
# ============================================================================
if [[ "$section" == "all" || "$section" == "cache" ]]; then
    echo -e "${GREEN}┌──────────────────────────────────────────────────────────────────┐${NC}"
    echo -e "${GREEN}│ AI RESPONSE CACHE                                                │${NC}"
    echo -e "${GREEN}└──────────────────────────────────────────────────────────────────┘${NC}"

    # Cache stats
    total_cache=$(mysql_cmd -e "SELECT COUNT(*) FROM alive_ai_response_cache" "$DB_CHARS")
    active_cache=$(mysql_cmd -e "SELECT COUNT(*) FROM alive_ai_response_cache WHERE expires_at > NOW()" "$DB_CHARS")
    total_hits=$(mysql_cmd -e "SELECT COALESCE(SUM(hit_count), 0) FROM alive_ai_response_cache" "$DB_CHARS")

    echo -e "Total cached responses: ${YELLOW}${total_cache:-0}${NC}"
    echo -e "Active (not expired): ${GREEN}${active_cache:-0}${NC}"
    echo -e "Total cache hits: ${CYAN}${total_hits:-0}${NC}"

    # Most used cache entries
    echo -e "\n${BLUE}Most Used Cache Entries:${NC}"
    mysql_table -e "
        SELECT request_type, hit_count, response_time_ms,
               created_at, expires_at
        FROM alive_ai_response_cache
        ORDER BY hit_count DESC
        LIMIT 10
    " "$DB_CHARS" 2>/dev/null || echo "  (No data)"
    echo ""
fi

# ============================================================================
# PERSONALITY PRESETS
# ============================================================================
if [[ "$section" == "all" || "$section" == "presets" ]]; then
    echo -e "${GREEN}┌──────────────────────────────────────────────────────────────────┐${NC}"
    echo -e "${GREEN}│ PERSONALITY PRESETS                                              │${NC}"
    echo -e "${GREEN}└──────────────────────────────────────────────────────────────────┘${NC}"

    mysql_table -e "
        SELECT name, description, aggression, caution, sociability, weight
        FROM alive_personality_presets
        ORDER BY weight DESC
    " "$DB_WORLD" 2>/dev/null || echo "  (No data)"
    echo ""
fi

# ============================================================================
# AI PROMPT TEMPLATES
# ============================================================================
if [[ "$section" == "all" || "$section" == "prompts" ]]; then
    echo -e "${GREEN}┌──────────────────────────────────────────────────────────────────┐${NC}"
    echo -e "${GREEN}│ AI PROMPT TEMPLATES                                              │${NC}"
    echo -e "${GREEN}└──────────────────────────────────────────────────────────────────┘${NC}"

    mysql_table -e "
        SELECT template_name, description, max_tokens, temperature, enabled
        FROM alive_ai_prompt_templates
    " "$DB_WORLD" 2>/dev/null || echo "  (No data)"
    echo ""
fi

echo -e "${CYAN}════════════════════════════════════════════════════════════════════${NC}"
echo -e "Run with section parameter: ${YELLOW}./alive_status.sh [bots|zones|gossip|cache|connections|presets|prompts]${NC}"
