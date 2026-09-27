/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>
 * Released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 *
 * EC Bot Goal System - Bot Personality Structure
 *
 * Defines personality traits that influence bot behavior and goal selection.
 */

#ifndef _EC_BOT_PERSONALITY_H
#define _EC_BOT_PERSONALITY_H

/**
 * BotPersonality - Traits that influence bot decision-making
 *
 * Each trait is a float from 0.0 to 1.0:
 * - 0.0 = minimal expression of trait
 * - 0.5 = neutral/average
 * - 1.0 = maximum expression of trait
 *
 * These traits influence:
 * - Goal selection and weighting
 * - Combat behavior (fight vs flee)
 * - Social interactions (help vs ignore)
 * - Economic decisions (save vs spend)
 */
struct BotPersonality
{
    /**
     * Aggression - Combat engagement tendency
     * High: Seeks combat, prefers offensive actions, takes risks
     * Low: Avoids combat, prefers defensive actions, cautious
     */
    float aggression;

    /**
     * Helpfulness - Willingness to assist others
     * High: Buffs party, heals others, shares loot
     * Low: Self-focused, prioritizes own needs
     */
    float helpfulness;

    /**
     * Chattiness - Communication frequency
     * High: Frequent emotes, party chat, social interactions
     * Low: Silent, minimal communication
     */
    float chattiness;

    /**
     * Curiosity - Exploration and learning tendency
     * High: Explores zones, learns recipes, tries new activities
     * Low: Sticks to known areas and routines
     */
    float curiosity;

    /**
     * Greed - Gold and item acquisition focus
     * High: Prioritizes valuable loot, gold farming, AH trading
     * Low: Content with minimal resources, generous
     */
    float greed;

    BotPersonality()
        : aggression(0.5f)
        , helpfulness(0.5f)
        , chattiness(0.5f)
        , curiosity(0.5f)
        , greed(0.5f)
    {}

    BotPersonality(float agg, float help, float chat, float cur, float grd)
        : aggression(agg)
        , helpfulness(help)
        , chattiness(chat)
        , curiosity(cur)
        , greed(grd)
    {}
};

#endif // _EC_BOT_PERSONALITY_H
