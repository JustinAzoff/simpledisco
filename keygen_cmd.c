#include "czmq_library.h"


// https://stackoverflow.com/questions/744766/how-to-compare-ends-of-strings-in-c
int EndsWith(const char *str, const char *suffix)
{
    if (!str || !suffix)
        return 0;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix >  lenstr)
        return 0;
    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}


int keygen_cmd(const char *filename)
{
    char *keypair_filename;
    const char *keypair_filename_secret;
    // The next bit of code ensures that keypair_filename is 'foo' and keypair_filename_secret is 'foo_secret'
    // FIXME: this leaks the filenames
    if(EndsWith(filename, "_secret")) {
        keypair_filename_secret = filename;
        keypair_filename = strdup(filename);
        char *ptr = keypair_filename + strlen(keypair_filename) - strlen("_secret");
        *ptr = '\0';
    } else {
        keypair_filename = (char *) filename;
        keypair_filename_secret = zsys_sprintf("%s_secret", filename);
    }

    if( access( keypair_filename, F_OK ) != -1 ) {
        zsys_info("%s already exists, not creating keys", keypair_filename);
        return 0;
    }
    if( access( keypair_filename_secret, F_OK ) != -1 ) {
        zsys_info("%s already exists, not creating keys", keypair_filename);
        return 0;
    }
    zcert_t *cert = zcert_new();
    if(!cert) {
        perror("Error creating new certificate");
        return 1;
    }
    if(-1 == zcert_save(cert, keypair_filename)) {
        zsys_info("Attempting to write keys to %s and %s", keypair_filename, keypair_filename_secret);
        perror("Error writing key");
        return 1;
    }
    zsys_info("Keys written to %s and %s", keypair_filename, keypair_filename_secret);
    return 0;
}
