#include "protocol.h"

#include "LL_Lib.h"
#include <elapsedMillis.h>

#include <FS.h>
#include <LittleFS.h>

#include <Crypto.h>
#include <Ed25519.h>
#include "c25519_22b.hpp"
#include <RNG.h>
#include "esp_random.h"

#include "userdb.h"
#include "network/lan.h"

#include "door.h"

#define PROTOMINLEN (4+64+32+32+8+1)

extern bool debugUnlocked;

bool addAllUnknownUsers = false;

const int maxChallenges = 8;
const uint32_t maxChallengeLife = 120000; // 2min

struct sChallenge {
  uint8_t challenge[8];
  uint8_t publicKey[32];
  uint32_t timestamp; // 0 = invalid challenge, otherwise millis
};

sChallenge Challenges[maxChallenges];

uint8_t lastUnknownPublicKey[32];
uint8_t lastUnknownName[32];


// Generate random challenge
void generateChallenge(uint8_t *challenge) {
  esp_fill_random(challenge, 8);
}

/**
 * Ckecks if challenge is valid, get new challenge
 * Returns new challenge in challenge[8]
 * Pubkey[32]
 */
bool getChallenge(uint8_t *challenge, const uint8_t *pubkey) {
  bool ret = false;
  
  int pubkeyindex = -1;

  // Scan through challenges, remove too old ones, find matching challenge
  for(int i=0; i<maxChallenges; i++) {
    if(Challenges[i].timestamp != 0) {
      if((millis() - Challenges[i].timestamp) > maxChallengeLife) Challenges[i].timestamp = 0;
    } 
    if(Challenges[i].timestamp != 0) {
      if(memcmp(Challenges[i].publicKey, pubkey, 32) == 0) {
        pubkeyindex = i;
        if(memcmp(Challenges[i].challenge, challenge, 8) == 0) {
          ret = true;
          // Get next challenge 
          generateChallenge(Challenges[i].challenge);
          memcpy(challenge, Challenges[i].challenge, 8);
          Challenges[i].timestamp = millis();
        }  
      }  
    }  
  }

  // Get and store new challenge
  if(ret == false) {
    // Slot for pubkey already available?
    if(pubkeyindex < 0) {
      // Try find empty slot or overwrite oldest timestamp
      uint32_t oldest = 0;
      for(int i=0; i<maxChallenges; i++) {
        if(Challenges[i].timestamp == 0) {
          pubkeyindex = i;
          break;
        } else {
          uint32_t age = millis() - Challenges[i].timestamp;
          if(age > oldest) {
            oldest = age;
            pubkeyindex = i;
          }
        }
      }
    }  
    memcpy(Challenges[pubkeyindex].publicKey, pubkey, 32);
    generateChallenge(Challenges[pubkeyindex].challenge);
    memcpy(challenge, Challenges[pubkeyindex].challenge, 8);
    Challenges[pubkeyindex].timestamp = millis();      
  }  

  return ret;
}

/**
 * data + len: input data, including preemble "D00r", all binary
 * answer + anslen: output data buffer, 250 bytes recommended.
 * sourceid: 0 espnow near, 1 websocket local, 2 espnow far
 * returns false when no answer is given because something is wrong.
 * 
 * Protocol Rx:
 * Off Len
 *   0   4  Preemble "D00r"
 *   4  64  Signature
 *  68  32  Public Key
 * 100  32  Name (Cleartext UTF8, 0 filled)
 * 132   8  Challenge respone
 * 140   1  Command (might be longer than 1 if needed)   
 * 
 * Protocol Tx:
 * Off Len
 *   0   4  Preemble "D00a" (for answer)
 *   4  32  Public Key from Rx
 *  36   8  New challenge
 *  44   1  Answer (might be longer than 1 if needed)
 */
bool protocolInput(uint8_t *data, size_t len, uint8_t *answer, size_t *anslen, int sourceid) {
  if((data == NULL) || (len < PROTOMINLEN) || (answer == NULL) || (*anslen < 90)) return false;
  if(!((data[0] == 'D') && (data[1] == '0') && (data[2] == '0') && (data[3] == 'r'))) return false;

  // ignore esp far for now...
  if((sourceid != 0) && (sourceid != 1)) return false;

  elapsedMillis perfMs;
  bool sigValid = false;
  bool challengeValid = false;
  String namestr = "";
  String userFlags = "";
  bool userFound = false;

  uint8_t *signature = &data[4];
  uint8_t *publicKey = &data[68];
  uint8_t *name      = &data[100];
  uint8_t *challenge = &data[132];
  uint8_t *command   = &data[140];

  answer[0] = 'D';
  answer[1] = '0';
  answer[2] = '0';
  answer[3] = 'a';
  answer[44] = 'E';
  *anslen = 45;
  memcpy(&answer[4], publicKey, 32);
  // Checks and generate new challenge
  uint8_t newChallenge[8];
  memcpy(newChallenge, challenge, 8);
  challengeValid = getChallenge(newChallenge, publicKey); 
  memcpy(&answer[36], newChallenge, 8);
  LL_Log.printf("challengeValid %s\n", challengeValid ? "True" : "False");
  LL_Log.printf("perf1: %d\n", (int)perfMs);

  if(command[0] == 'c') {
    // do not check signature to safe time
    answer[44] = 'c'; // getting a challenge does not require much...
  } else {
    // Verify signature
    sigValid = Ed25519::verify(signature, publicKey, publicKey, len-68);
    LL_Log.printf("perf2: %d\n", (int)perfMs);
    //bool sigValid2 = edsign_verify(signature, publicKey, publicKey, len-68);
    //LL_Log.printf("perf3: %d\n", (int)perfMs);
    LL_Log.printf("sigValid %s\n", sigValid ? "True" : "False");
    //LL_Log.printf("sigValid2 %s\n", sigValid2 ? "True" : "False");

    // Lookup public key
    char buff[4]; String msg = "";
    for(size_t i=0; i < 32; i++) {
      sprintf(buff, "%02x", (uint8_t) publicKey[i]);
      msg += buff ;
    }
    LL_Log.printf("Pubkey: %s\n",msg.c_str());

    // Lookup user
    userFound = UserDB.findUser(msg.c_str(), namestr, userFlags);
    LL_Log.printf("userFound %s\n", userFound ? "True" : "False");
    LL_Log.printf("perf5: %d\n", (int)perfMs);  
  
    if(sigValid) { 
      if(!userFound) {
        if(command[0] == 't') {
          // If unkonwn user sends 't' it get temporarily stored for review
          memcpy(lastUnknownPublicKey, publicKey, 32);
          memcpy(lastUnknownName, name, 32);
        }  
        *anslen += sprintf((char *)&answer[45], " User not found!");
        if(addAllUnknownUsers == true) {
          UserDB.addUser(msg.c_str(), (const char *)name, "");
          UserDB.saveUserData();
          LL_Log.println(" User auto added.");
        }
      } else {
        if(challengeValid) {
          // All others must be in database and need enough rights
          // stCOURaA
          if((command[0] == 's') && (userFlags.indexOf('s') != -1)) {
            answer[44] = 's'; 
            answer[45] = (uint8_t)('0' + doorGetDoorState()); 
            answer[46] = (uint8_t)('0' + doorGetLockState()); 
            answer[47] = (uint8_t)('0' + doorGetTrigState()); 
            answer[48] = 0; *anslen = 48;
            LL_Log.printf(" Get Status: %s\r\n", (const char *)&answer[45]);
          } else if((command[0] == 't') && (userFlags.indexOf('t') != -1)) {
            answer[44] = 't';
            LL_Log.println(" Triggered.");
            doorAction(1); // 2
          } else if((command[0] == 'C') && (userFlags.indexOf('C') != -1)) {
            answer[44] = 'C';
            LL_Log.println(" Immediately close.");
            doorAction(-1);    
          } else if((command[0] == 'O') && (userFlags.indexOf('O') != -1)) {
            answer[44] = 'O';
            LL_Log.println(" Open.");
            doorAction(1);        
          } else if((command[0] == 'U') && (userFlags.indexOf('U') != -1)) {
            answer[44] = 'U';
            LL_Log.println(" UNLOCK DEBUG. DON'T FORGET TO RESET.");
            *anslen += sprintf((char *)&answer[45], " UNLOCK DEBUG. DON'T FORGET TO RESET.");
            lanEnableWebFileEditor();
            debugUnlocked = true;
          } else if((command[0] == 'R') && (userFlags.indexOf('R') != -1)) {
            answer[44] = 'R';
            ESP.restart();
            // will this here still reached?
          } else if((command[0] == 'a') && (userFlags.indexOf('a') != -1)) {
            answer[44] = 'a';
            LL_Log.println(" Stop adding all unknown users.");
            *anslen += sprintf((char *)&answer[45], " Stop adding all unknown users.");
            addAllUnknownUsers = false;      
          } else if((command[0] == 'A') && (userFlags.indexOf('A') != -1)) {
            answer[44] = 'A';
            LL_Log.println(" Add all unknown users.");
            *anslen += sprintf((char *)&answer[45], " Add all unknown users.");
            addAllUnknownUsers = true;         
          } else {
            *anslen += sprintf((char *)&answer[45], " Not enough rights!");
          }
        } else {
          *anslen += sprintf((char *)&answer[45], " Invalid challenge!");
        }
      } 
    } else {
      *anslen += sprintf((char *)&answer[45], " Invalid signature!");
    } 
  }

  return true;
}  

