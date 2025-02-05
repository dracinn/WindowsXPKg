//
// Created by Andrew on 01/06/2023.
//

#include "cli.h"
#include "confid.h"
#include "BINK1998.h"
#include "BINK2002.h"

bool CLI::loadJSON(const fs::path& filename, json *output) {
    if (!fs::exists(filename)) {
        fmt::print("ERROR: File {} does not exist\n", filename.string());
        return false;
    }

    std::ifstream f(filename);
    *output = json::parse(f, nullptr, false, false);

    if (output->is_discarded()) {
        fmt::print("ERROR: Unable to parse keys from {}\n", filename.string());
        return false;
    }

    return true;
}


void CLI::showHelp(char *argv[]) {
    fmt::print("usage: {} \n", argv[0]);
    fmt::print("\t-h --help\tshow this message\n");
    fmt::print("\t-v --verbose\tenable verbose output\n");
    fmt::print("\t-n --number\tnumber of keys to generate (defaults to 1)\n");
    fmt::print("\t-f --file\tspecify which keys file to load (defaults to keys.json)\n");
    fmt::print("\t-i --instid\tinstallation ID used to generate confirmation ID\n");
    fmt::print("\t-b --binkid\tspecify which BINK identifier to load (defaults to 2E)\n");
    fmt::print("\t-l --list\tshow which products/binks can be loaded\n");
    fmt::print("\t-c --channelid\tspecify which Channel Identifier to use (defaults to 640)\n");
    fmt::print("\n\n");
}

int CLI::parseCommandLine(int argc, char* argv[], Options* options) {
    *options = Options {
            "2E",
            "keys.json",
            "",
            640,
            1,
            false,
            false,
            false,
            false,
            MODE_BINK1998
    };
    // set default options

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-v" || arg == "--verbose") {
            options->verbose = true;
        } else if (arg == "-h" || arg == "--help") {
            options->help = true;
        } else if (arg == "-n" || arg == "--number") {
            if (i == argc - 1) {
                options->error = true;
                break;
            }

            int nKeys;
            if (!sscanf(argv[i+1], "%d", &nKeys)) {
                options->error = true;
            } else {
                options->numKeys = nKeys;
            }
            i++;
        } else if (arg == "-b" || arg == "--bink") {
            if (i == argc - 1) {
                options->error = true;
                break;
            }

            options->binkid = argv[i+1];
            i++;
        } else if (arg == "-l" || arg == "--list") {
            options->list = true;
        } else if (arg == "-c" || arg == "--channelid") {
            if (i == argc - 1) {
                options->error = true;
                break;
            }

            int siteID;
            if (!sscanf(argv[i+1], "%d", &siteID)) {
                options->error = true;
            } else {
                options->channelID = siteID;
            }
            i++;
        } else if (arg == "-f" || arg == "--file") {
            if (i == argc - 1) {
                options->error = true;
                break;
            }

            options->keysFilename = argv[i+1];
            i++;
        } else if (arg == "-i" || arg == "--instid") {
            if (i == argc - 1) {
                options->error = true;
                break;
            }

            options->instid = argv[i+1];
            options->applicationMode = MODE_CONFIRMATION_ID;
            i++;
        } else {
            options->error = true;
        }
    }

    return !options->error;
}

int CLI::validateCommandLine(Options* options, char *argv[], json *keys) {
    if (options->verbose) {
        fmt::print("Loading keys file {}\n", options->keysFilename);
    }

    if (!loadJSON(options->keysFilename, keys)) {
        return 2;
    }

    if (options->verbose) {
        fmt::print("Loaded keys from {} successfully\n",options->keysFilename);
    }

    if (options->help || options->error) {
        if (options->error) {
            fmt::print("error parsing command line options\n");
        }
        showHelp(argv);
        return 1;
    }

    if (options->list) {
        for (auto el : (*keys)["Products"].items()) {
            int id;
            sscanf((el.value()["BINK"][0]).get<std::string>().c_str(), "%x", &id);
            std::cout << el.key() << ": " << el.value()["BINK"] << std::endl;
        }

        fmt::print("\n\n");
        fmt::print("** Please note: any BINK ID other than 2E is considered experimental at this time **\n");
        fmt::print("\n");
        return 1;
    }

    int intBinkID;
    sscanf(options->binkid.c_str(), "%x", &intBinkID);

    if (intBinkID >= 0x40) {
        options->applicationMode = MODE_BINK2002;
    }

    if (options->channelID > 999) {
        fmt::print("ERROR: refusing to create a key with a Channel ID greater than 999\n");
        return 1;
    }

    return 0;
}

void CLI::printID(DWORD *pid)
{
    char raw[12];
    char b[6], c[8];
    int i, digit = 0;

    //	Cut away last bit of pid and convert it to an accii-number (=raw)
    sprintf(raw, "%iu", pid[0] >> 1);

    // Make b-part {640-....}
    strncpy(b, raw, 3);
    b[3] = 0;

    // Make c-part {...-123456X...}
    strcpy(c, raw + 3);
    fmt::print("> {}\n", c);

    // Make checksum digit-part {...56X-}
    assert(strlen(c) == 6);
    for (i = 0; i < 6; i++)
        digit -= c[i] - '0';	// Sum digits

    while (digit < 0)
        digit += 7;
    c[6] = digit + '0';
    c[7] = 0;

    fmt::print("Product ID: PPPPP-{}-{}-23xxx\n", b, c);
}

void CLI::printKey(char *pk) {
    assert(strlen(pk) == 25);

    std::string spk = pk;
    fmt::print("{}-{}-{}-{}-{}\n",
               spk.substr(0,5),
               spk.substr(5,5),
               spk.substr(10,5),
               spk.substr(15,5),
               spk.substr(20,5));
}

CLI::CLI(Options options, json keys) {
    this->options = options;
    this->keys = keys;

    this->BINKID = options.binkid.c_str();

    // We cannot produce a valid key without knowing the private key k. The reason for this is that
    // we need the result of the function K(x; y) = kG(x; y).
    this->privateKey = BN_new();

    // We can, however, validate any given key using the available public key: {p, a, b, G, K}.
    // genOrder the order of the generator G, a value we have to reverse -> Schoof's Algorithm.
    this->genOrder = BN_new();

    /* Computed data */
    BN_dec2bn(&this->genOrder,   this->keys["BINK"][this->BINKID]["n"].   get<std::string>().c_str());
    BN_dec2bn(&this->privateKey, this->keys["BINK"][this->BINKID]["priv"].get<std::string>().c_str());

    if (options.verbose) {
        fmt::print("----------------------------------------------------------- \n");
        fmt::print("Loaded the following elliptic curve parameters: BINK[{}]\n", this->BINKID);
        fmt::print("----------------------------------------------------------- \n");
        fmt::print(" P: {}\n", this->keys["BINK"][this->BINKID]["p"].get<std::string>());
        fmt::print(" a: {}\n", this->keys["BINK"][this->BINKID]["a"].get<std::string>());
        fmt::print(" b: {}\n", this->keys["BINK"][this->BINKID]["b"].get<std::string>());
        fmt::print("Gx: {}\n", this->keys["BINK"][this->BINKID]["g"]["x"].get<std::string>());
        fmt::print("Gy: {}\n", this->keys["BINK"][this->BINKID]["g"]["y"].get<std::string>());
        fmt::print("Kx: {}\n", this->keys["BINK"][this->BINKID]["pub"]["x"].get<std::string>());
        fmt::print("Ky: {}\n", this->keys["BINK"][this->BINKID]["pub"]["y"].get<std::string>());
        fmt::print(" n: {}\n", this->keys["BINK"][this->BINKID]["n"].get<std::string>());
        fmt::print(" k: {}\n", this->keys["BINK"][this->BINKID]["priv"].get<std::string>());
        fmt::print("\n");
    }

    eCurve = initializeEllipticCurve(
            this->keys["BINK"][this->BINKID]["p"].get<std::string>(),
            this->keys["BINK"][this->BINKID]["a"].get<std::string>(),
            this->keys["BINK"][this->BINKID]["b"].get<std::string>(),
            this->keys["BINK"][this->BINKID]["g"]["x"].get<std::string>(),
            this->keys["BINK"][this->BINKID]["g"]["y"].get<std::string>(),
            this->keys["BINK"][this->BINKID]["pub"]["x"].get<std::string>(),
            this->keys["BINK"][this->BINKID]["pub"]["y"].get<std::string>(),
            this->genPoint,
            this->pubPoint
    );

    this->count = 0;
    this->total = this->options.numKeys;
}

int CLI::BINK1998() {
    DWORD nRaw = this->options.channelID * 1'000'000 ; /* <- change */

    BIGNUM *bnrand = BN_new();
    BN_rand(bnrand, 19, BN_RAND_TOP_ANY, BN_RAND_BOTTOM_ANY);

    int oRaw;
    char *cRaw = BN_bn2dec(bnrand);

    sscanf(cRaw, "%d", &oRaw);
    nRaw += (oRaw % 999999); // ensure our serial is less than 999999

    if (this->options.verbose) {
        fmt::print("> PID: {:09d}\n", nRaw);
    }

    // generate a key
    BN_sub(this->privateKey, this->genOrder, this->privateKey);

    // Specify whether an upgrade version or not
    bool bUpgrade = false;

    for (int i = 0; i < this->total; i++) {
        BINK1998::Generate(this->eCurve, this->genPoint, this->genOrder, this->privateKey, nRaw, bUpgrade, this->pKey);
        CLI::printKey(this->pKey);
        fmt::print("\n");

        // verify the key
        this->count += BINK1998::Verify(this->eCurve, this->genPoint, this->pubPoint, this->pKey);
    }

    fmt::print("Success count: {}/{}\n", this->count, this->total);
    return 0;
}

int CLI::BINK2002() {
    DWORD pChannelID = this->options.channelID;

    if (this->options.verbose) {
        fmt::print("> Channel ID: {:03d}\n", this->options.channelID);
    }

    // generate a key
    for (int i = 0; i < this->total; i++) {
        DWORD pAuthInfo;
        RAND_bytes((BYTE *)&pAuthInfo, 4);
        pAuthInfo &= BITMASK(10);

        if (this->options.verbose) {
            fmt::print("> AuthInfo: {}\n", pAuthInfo);
        }

        BINK2002::Generate(this->eCurve, this->genPoint, this->genOrder, this->privateKey, pChannelID, pAuthInfo, false, this->pKey);
        CLI::printKey(this->pKey);
        fmt::print("\n\n");

        // verify a key
        this->count += BINK2002::Verify(this->eCurve, this->genPoint, this->pubPoint, this->pKey);
    }

    fmt::print("Success count: {}/{}\n", this->count, this->total);
    return 0;
}

int CLI::ConfirmationID() {
    char confirmation_id[49];
    int err = ConfirmationID::Generate(this->options.instid.c_str(), confirmation_id);

    switch (err) {
        case ERR_TOO_SHORT:
            fmt::print("ERROR: Installation ID is too short.\n");
            return 1;

        case ERR_TOO_LARGE:
            fmt::print("ERROR: Installation ID is too long.\n");
            return 1;

        case ERR_INVALID_CHARACTER:
            fmt::print("ERROR: Invalid character in installation ID.\n");
            return 1;

        case ERR_INVALID_CHECK_DIGIT:
            fmt::print("ERROR: Installation ID checksum failed. Please check that it is typed correctly.\n");
            return 1;

        case ERR_UNKNOWN_VERSION:
            fmt::print("ERROR: Unknown installation ID version.\n");
            return 1;

        case ERR_UNLUCKY:
            fmt::print("ERROR: Unable to generate valid confirmation ID.\n");
            return 1;

        case SUCCESS:
            fmt::print("Confirmation ID: {}\n", confirmation_id);
            return 0;

        default:
            fmt::print("Unknown error occurred during Confirmation ID generation: {}\n", err);
    }
    return 1;
}


