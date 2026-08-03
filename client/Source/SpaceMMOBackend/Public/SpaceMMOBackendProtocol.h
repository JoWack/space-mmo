#pragma once

#include "CoreMinimal.h"
#include "SpaceMMOBackendTypes.h"

/**
 * Turning bytes into game state, and back.
 *
 * Pure functions over strings, knowing nothing about HTTP, sockets, or the engine's request
 * machinery. That is deliberate: parsing is where a protocol actually goes wrong, and this way
 * every failure mode — a truncated body, a missing field, a number that does not fit, a hostile
 * response — can be tested without a server, a socket, or a running game.
 *
 * The transport is thin enough to read in one sitting; this is the part with the bugs in it.
 */
class SPACEMMOBACKEND_API FSpaceMMOBackendProtocol
{
public:
	/** Joins a base URL and a path with exactly one slash between them. */
	static FString JoinUrl(const FString& BaseUrl, const FString& Path);

	/** JSON body for a register or login request. */
	static FString MakeCredentialsBody(const FString& Email, const FString& Password);

	/** JSON body for character creation. */
	static FString MakeCreateCharacterBody(const FString& Name, EBackendRace Race);

	/**
	 * Parses a session response.
	 *
	 * @return False if any required field is missing or the token is empty. A session without a
	 *         usable token would otherwise be stored and every later request would 401 for
	 *         reasons that appear to have nothing to do with logging in.
	 */
	static bool ParseSession(const FString& Json, FBackendSession& OutSession);

	/** Parses one character object. */
	static bool ParseCharacter(const TSharedPtr<class FJsonObject>& Object, FBackendCharacter& OutCharacter);

	/** Parses a JSON array of characters. */
	static bool ParseCharacterList(const FString& Json, TArray<FBackendCharacter>& OutCharacters);

	/** Parses a JSON array of skills. */
	static bool ParseSkills(const FString& Json, TArray<FBackendSkill>& OutSkills);

	/** Parses a JSON array of inventory stacks. */
	static bool ParseInventory(const FString& Json, TArray<FBackendInventoryItem>& OutItems);

	/** Parses a JSON array of bodies. */
	static bool ParseBodies(const FString& Json, TArray<FBackendBody>& OutBodies);

	/**
	 * Parses a JSON array of resource deposits.
	 *
	 * A deposit whose direction is missing, unparseable, or the zero vector is dropped rather than
	 * defaulted. There is no sensible default for "which way from the centre" — a zero vector names
	 * no point on the sphere at all — and the alternative to dropping it is a deposit sitting at the
	 * planet's core where nobody can reach it and no error was ever reported.
	 */
	static bool ParseResourceNodes(const FString& Json, TArray<FBackendResourceNode>& OutNodes);

	/**
	 * Classifies a response.
	 *
	 * @param HttpStatus Zero when the request never reached the server.
	 *
	 * Status alone decides the category; the body only supplies a message. A server that returns
	 * 200 with an error in the body would be a server bug, and treating the body as authoritative
	 * would hide it.
	 */
	static FBackendFailure ClassifyFailure(int32 HttpStatus, const FString& Body);

	/**
	 * Pulls a human-readable message out of an error body.
	 *
	 * Handles both shapes the API returns: <c>{"error": "..."}</c> from explicit conflicts, and
	 * RFC 7807 problem details, which use <c>title</c> and <c>detail</c>.
	 */
	static FString ExtractErrorMessage(const FString& Body);

private:
	/**
	 * Reads an int64 from a JSON field.
	 *
	 * JSON numbers are doubles, and a double holds only 53 bits of integer exactly. Balances are
	 * int64 minor units, so a large enough balance would silently round on the way in. Numbers
	 * beyond the exact range are therefore rejected rather than quietly mangled, and the server
	 * is free to send them as strings when it comes to that.
	 */
	static bool ReadInt64(const TSharedPtr<FJsonObject>& Object, const FString& Field, int64& OutValue);
};
