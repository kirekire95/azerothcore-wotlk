#!/bin/bash
#
# EC World Events - Database Status Script
# Shows the current state of all EC World Events tables
#

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Database credentials (adjust as needed)
DB_CONTAINER="${DB_CONTAINER:-ab73902023df_ac-database}"
DB_USER="${DB_USER:-root}"
DB_PASS="${DB_PASS:-password}"

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}  EC World Events - Database Status${NC}"
echo -e "${CYAN}========================================${NC}"
echo ""

# Function to run MySQL query
run_query() {
    local db=$1
    local query=$2
    docker exec "$DB_CONTAINER" mysql -u"$DB_USER" -p"$DB_PASS" "$db" -e "$query" 2>/dev/null
}

# Check connection
echo -e "${YELLOW}Checking database connection...${NC}"
if ! docker exec "$DB_CONTAINER" mysql -u"$DB_USER" -p"$DB_PASS" -e "SELECT 1" &>/dev/null; then
    echo -e "${RED}ERROR: Cannot connect to database!${NC}"
    echo "Make sure the database container is running: docker ps"
    exit 1
fi
echo -e "${GREEN}Connected successfully${NC}"
echo ""

# Check tables exist
echo -e "${YELLOW}=== Tables Status ===${NC}"

# World database tables
echo -e "\n${CYAN}World Database (acore_world):${NC}"
WORLD_TABLES=$(docker exec "$DB_CONTAINER" mysql -u"$DB_USER" -p"$DB_PASS" -N acore_world -e "SHOW TABLES LIKE 'ec_%'" 2>/dev/null)
if [ -z "$WORLD_TABLES" ]; then
    echo -e "  ${RED}No EC tables found!${NC}"
else
    for table in $WORLD_TABLES; do
        count=$(docker exec "$DB_CONTAINER" mysql -u"$DB_USER" -p"$DB_PASS" -N acore_world -e "SELECT COUNT(*) FROM $table" 2>/dev/null)
        echo -e "  ${GREEN}✓${NC} $table: $count rows"
    done
fi

# Characters database tables
echo -e "\n${CYAN}Characters Database (acore_characters):${NC}"
CHAR_TABLES=$(docker exec "$DB_CONTAINER" mysql -u"$DB_USER" -p"$DB_PASS" -N acore_characters -e "SHOW TABLES LIKE 'ec_%'" 2>/dev/null)
if [ -z "$CHAR_TABLES" ]; then
    echo -e "  ${RED}No EC tables found!${NC}"
else
    for table in $CHAR_TABLES; do
        count=$(docker exec "$DB_CONTAINER" mysql -u"$DB_USER" -p"$DB_PASS" -N acore_characters -e "SELECT COUNT(*) FROM $table" 2>/dev/null)
        echo -e "  ${GREEN}✓${NC} $table: $count rows"
    done
fi

# Event Definitions
echo -e "\n${YELLOW}=== Event Definitions ===${NC}"
run_query "acore_world" "SELECT id, name,
    CASE type
        WHEN 0 THEN 'Raid'
        WHEN 1 THEN 'Defense'
        WHEN 2 THEN 'WorldBoss'
        WHEN 3 THEN 'Gathering'
        WHEN 4 THEN 'PvPZone'
    END as type,
    CASE attacker_faction
        WHEN 0 THEN 'Neutral'
        WHEN 1 THEN 'Alliance'
        WHEN 2 THEN 'Horde'
    END as attackers,
    location_name,
    CONCAT(min_level, '-', max_level) as level_range,
    CONCAT(min_bots, '-', max_bots) as bots,
    CONCAT(duration_minutes, ' min') as duration,
    IF(enabled=1, 'Yes', 'No') as enabled
FROM ec_world_events ORDER BY id;"

# Event Rewards
echo -e "\n${YELLOW}=== Reward Definitions ===${NC}"
run_query "acore_characters" "SELECT
    r.event_id,
    e.name as event_name,
    CASE r.reward_type
        WHEN 0 THEN 'Gold'
        WHEN 1 THEN 'Honor'
        WHEN 2 THEN 'Item'
    END as type,
    CASE r.reward_type
        WHEN 0 THEN CONCAT(ROUND(r.value/10000), 'g')
        ELSE r.value
    END as value,
    r.min_kills,
    CONCAT(r.participation_percent, '%') as min_participation
FROM ec_world_event_rewards r
LEFT JOIN acore_world.ec_world_events e ON r.event_id = e.id
ORDER BY r.event_id, r.id;"

# Recent Event Logs
echo -e "\n${YELLOW}=== Recent Event Logs (last 10) ===${NC}"
LOG_COUNT=$(docker exec "$DB_CONTAINER" mysql -u"$DB_USER" -p"$DB_PASS" -N acore_characters -e "SELECT COUNT(*) FROM ec_world_event_logs" 2>/dev/null)
if [ "$LOG_COUNT" -eq 0 ]; then
    echo -e "  ${CYAN}No events have been run yet.${NC}"
else
    run_query "acore_characters" "SELECT
        l.instance_id,
        e.name as event_name,
        CASE l.state
            WHEN 0 THEN 'Scheduled'
            WHEN 1 THEN 'Active'
            WHEN 2 THEN 'Completed'
            WHEN 3 THEN 'Cancelled'
            WHEN 4 THEN 'Failed'
        END as state,
        l.total_kills,
        l.participant_count,
        l.start_time,
        l.end_time
    FROM ec_world_event_logs l
    LEFT JOIN acore_world.ec_world_events e ON l.event_id = e.id
    ORDER BY l.start_time DESC LIMIT 10;"
fi

# Recent Participants
echo -e "\n${YELLOW}=== Recent Participants (last 20) ===${NC}"
PART_COUNT=$(docker exec "$DB_CONTAINER" mysql -u"$DB_USER" -p"$DB_PASS" -N acore_characters -e "SELECT COUNT(*) FROM ec_world_event_participants" 2>/dev/null)
if [ "$PART_COUNT" -eq 0 ]; then
    echo -e "  ${CYAN}No participants recorded yet.${NC}"
else
    run_query "acore_characters" "SELECT
        instance_id,
        player_name,
        kills,
        deaths,
        damage_dealt,
        healing_done,
        IF(rewarded=1, 'Yes', 'No') as rewarded,
        join_time
    FROM ec_world_event_participants
    ORDER BY join_time DESC LIMIT 20;"
fi

# Schedules
echo -e "\n${YELLOW}=== Event Schedules ===${NC}"
SCHED_COUNT=$(docker exec "$DB_CONTAINER" mysql -u"$DB_USER" -p"$DB_PASS" -N acore_world -e "SELECT COUNT(*) FROM ec_world_event_schedules" 2>/dev/null)
if [ "$SCHED_COUNT" -eq 0 ]; then
    echo -e "  ${CYAN}No schedules configured. Events can be started manually with .ecevent start <id>${NC}"
else
    run_query "acore_world" "SELECT
        s.id,
        e.name as event_name,
        CASE s.day_of_week
            WHEN 0 THEN 'Sunday'
            WHEN 1 THEN 'Monday'
            WHEN 2 THEN 'Tuesday'
            WHEN 3 THEN 'Wednesday'
            WHEN 4 THEN 'Thursday'
            WHEN 5 THEN 'Friday'
            WHEN 6 THEN 'Saturday'
            WHEN 7 THEN 'Every Day'
        END as day,
        s.hour,
        s.minute,
        IF(s.enabled=1, 'Yes', 'No') as enabled
    FROM ec_world_event_schedules s
    LEFT JOIN ec_world_events e ON s.event_id = e.id
    ORDER BY s.day_of_week, s.hour, s.minute;"
fi

echo -e "\n${CYAN}========================================${NC}"
echo -e "${CYAN}  Status check complete${NC}"
echo -e "${CYAN}========================================${NC}"
