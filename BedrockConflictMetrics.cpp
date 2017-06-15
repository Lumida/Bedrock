#include "BedrockConflictMetrics.h"

// Initialize non-const static variables.
recursive_mutex BedrockConflictMetrics::_mutex;
map<string, BedrockConflictMetrics> BedrockConflictMetrics::_conflictInfoMap;

BedrockConflictMetrics::BedrockConflictMetrics(const string& commandName) :
_commandName(commandName)
{ }

void BedrockConflictMetrics::success() {
    ++_totalSuccessCount;
    _results.reset(_resultsPtr);
    ++_resultsPtr;
    _resultsPtr %= COMMAND_COUNT;
}

void BedrockConflictMetrics::conflict() {
    ++_totalConflictCount;
    _results.set(_resultsPtr);
    ++_resultsPtr;
    _resultsPtr %= COMMAND_COUNT;
}

int BedrockConflictMetrics::recentSuccessCount() {
    return COMMAND_COUNT - _results.count();
}

int BedrockConflictMetrics::recentConflictCount() {
    return _results.count();
}

uint64_t BedrockConflictMetrics::totalSuccessCount() {
    return _totalSuccessCount;
}

uint64_t BedrockConflictMetrics::totalConflictCount() {
    return _totalConflictCount;
}

void BedrockConflictMetrics::recordConflict(const string& commandName) {
    SAUTOLOCK(_mutex);

    // Look up, and create if necessary, the info object for this command.
    auto it = _conflictInfoMap.find(commandName);
    if (it == _conflictInfoMap.end()) {
        // We need to insert this command.
        auto itPair = _conflictInfoMap.emplace(make_pair(commandName, commandName));
        it = itPair.first;
    }
    it->second.conflict();
    SINFO("Multi-write conflict recorded for " << commandName);
}

void BedrockConflictMetrics::recordSuccess(const string& commandName) {
    SAUTOLOCK(_mutex);

    // Look up, and create if necessary, the info object for this command.
    auto it = _conflictInfoMap.find(commandName);
    if (it == _conflictInfoMap.end()) {
        // We need to insert this command.
        auto itPair = _conflictInfoMap.emplace(make_pair(commandName, commandName));
        it = itPair.first;
    }
    it->second.success();
    SINFO("Multi-write success recorded for " << commandName);
}

bool BedrockConflictMetrics::multiWriteOK(const string& commandName) {
    SAUTOLOCK(_mutex);
    bool result;

    // Look up the command in the list of commands.
    auto it = _conflictInfoMap.find(commandName);
    if (it == _conflictInfoMap.end()) {
        // If we don't find it, we assume it's OK to multi-write.
        SINFO("Multi-write command '" << commandName << "' not tracked in BedrockConflictMetrics. Assuming OK.");
        result = true;
    } else {
        // Otherwise, we check to see if it's recent conflict count is too high for multi-write.
        auto& metric = it->second;
        int conflicts = metric.recentConflictCount();
        uint64_t totalAttempts = metric.totalConflictCount() + metric.totalSuccessCount();
        result = conflicts < THRESHOLD;
        string resultString = result ? "OK" : "DENIED";
        SINFO("Multi-write " << resultString << " for command '" << commandName << "' recent conflicts: "
              << conflicts << "/" << min((uint64_t)COMMAND_COUNT, totalAttempts) << ".");

        // And now that we know whether or not we can multi-write this, see if that's different than the last time we
        // checked for this command, so we can do extra logging if so.
        if (result != metric._lastCheckOK) {
            SINFO("Multi-write changing to " << resultString << " for command '" << commandName
                  << "' recent conflicts: " << conflicts << "/" << min((uint64_t)COMMAND_COUNT, totalAttempts)
                  << ", total conflicts: " << metric.totalConflictCount() << "/" << totalAttempts << ".");
        }
        metric._lastCheckOK = result;
    }
    return result;
}
