// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "OnlineSubsystemSteamPrivatePCH.h"
#include "OnlineSubsystemSteam.h"
#include "ModuleManager.h"

IMPLEMENT_MODULE(FOnlineSubsystemSteamModule, OnlineSubsystemSteam);

//HACKTASTIC (Needed to keep delete function from being stripped out and crashing when protobuffers deallocate memory)
void* HackDeleteFunctionPointer = (void*)(void(*)(void*))(::operator delete[]);

/**
 * Class responsible for creating instance(s) of the subsystem
 */
class FOnlineFactorySteam : public IOnlineFactory
{

private:

	/** Single instantiation of the STEAM interface */
	static FOnlineSubsystemSteamPtr SteamSingleton;

	virtual void DestroySubsystem()
	{
		if (SteamSingleton.IsValid())
		{
			SteamSingleton->Shutdown();
			SteamSingleton = NULL;
		}
	}

public:

	FOnlineFactorySteam() {}
	virtual ~FOnlineFactorySteam() 
	{
		DestroySubsystem();
	}

	virtual IOnlineSubsystemPtr CreateSubsystem(FName InstanceName)
	{
		if (!SteamSingleton.IsValid())
		{
			SteamSingleton = MakeShareable(new FOnlineSubsystemSteam(InstanceName));
			if (SteamSingleton->IsEnabled())
			{
				if(!SteamSingleton->Init())
				{
					UE_LOG_ONLINE(Warning, TEXT("Steam API failed to initialize!"));
					DestroySubsystem();
				}
			}
			else
			{
				UE_LOG_ONLINE(Warning, TEXT("Steam API disabled!"));
				DestroySubsystem();
			}

			return SteamSingleton;
		}

		UE_LOG_ONLINE(Warning, TEXT("Can't create more than one instance of Steam online subsystem!"));
		return NULL;
	}
};

FOnlineSubsystemSteamPtr FOnlineFactorySteam::SteamSingleton = NULL;

bool FOnlineSubsystemSteamModule::AreSteamDllsLoaded() const
{
	bool bLoadedClientDll = true;
	bool bLoadedServerDll = true;

#if LOADING_STEAM_LIBRARIES_DYNAMICALLY
	bLoadedClientDll = (SteamDLLHandle != NULL) ? true : false;
	#if LOADING_STEAM_SERVER_LIBRARY_DYNAMICALLY
	bLoadedServerDll = IsRunningDedicatedServer() ? ((SteamServerDLLHandle != NULL) ? true : false) : true;
	#endif //LOADING_STEAM_SERVER_LIBRARY_DYNAMICALLY
#endif // LOADING_STEAM_LIBRARIES_DYNAMICALLY

	return bLoadedClientDll && bLoadedServerDll;
}

static FString GetSteamModulePath()
{
#if PLATFORM_WINDOWS

	#if PLATFORM_64BITS
		return FPaths::EngineDir() / STEAM_SDK_ROOT_PATH / STEAM_SDK_VER_PATH / TEXT("Win64/");
	#else
		return FPaths::EngineDir() / STEAM_SDK_ROOT_PATH / STEAM_SDK_VER_PATH / TEXT("Win32/");
	#endif	//PLATFORM_64BITS

#else

	return FString();

#endif	//PLATFORM_WINDOWS
}

void FOnlineSubsystemSteamModule::LoadSteamModules()
{
	UE_LOG_ONLINE(Display, TEXT("Loading Steam SDK %s"), STEAM_SDK_VER);

#if PLATFORM_WINDOWS
	#if PLATFORM_64BITS
		FString RootSteamPath = GetSteamModulePath();
		FPlatformProcess::PushDllDirectory(*RootSteamPath);
		SteamDLLHandle = FPlatformProcess::GetDllHandle(*(RootSteamPath + "steam_api64.dll"));
#if 0 //64 bit not supported well at present, use Steam Client dlls
		// [RCL] 2015-02-09 the above comment ("64 bit not supported well...") is from (early) 2012, things might have changed
		// Load the Steam dedicated server dlls (assumes no Steam Client running)
		if (IsRunningDedicatedServer())
		{
			SteamServerDLLHandle = FPlatformProcess::GetDllHandle(*(RootSteamPath + "steamclient64.dll"));
		}
#endif 
		FPlatformProcess::PopDllDirectory(*RootSteamPath);
	#else	//PLATFORM_64BITS
		FString RootSteamPath = GetSteamModulePath();
		FPlatformProcess::PushDllDirectory(*RootSteamPath);
		SteamDLLHandle = FPlatformProcess::GetDllHandle(*(RootSteamPath + "steam_api.dll"));
		if (IsRunningDedicatedServer())
		{
			SteamServerDLLHandle = FPlatformProcess::GetDllHandle(*(RootSteamPath + "steamclient.dll"));
		}
		FPlatformProcess::PopDllDirectory(*RootSteamPath);
	#endif	//PLATFORM_64BITS
#elif PLATFORM_MAC
	SteamDLLHandle = FPlatformProcess::GetDllHandle(TEXT("libsteam_api.dylib"));
#elif PLATFORM_LINUX

	UE_LOG_ONLINE(Log, TEXT("Loading system libsteam_api.so."));
	SteamDLLHandle = FPlatformProcess::GetDllHandle(TEXT("libsteam_api.so"));
	if (SteamDLLHandle == nullptr)
	{
		// try bundled one
		UE_LOG_ONLINE(Log, TEXT("Could not find system one, loading bundled libsteam_api.so."));
		FString RootSteamPath = FPaths::EngineDir() / FString::Printf(TEXT("Binaries/ThirdParty/Steamworks/%s/Linux/"), STEAM_SDK_VER); 
		SteamDLLHandle = FPlatformProcess::GetDllHandle(*(RootSteamPath + "libsteam_api.so"));
	}

#endif	//PLATFORM_WINDOWS
}

void FOnlineSubsystemSteamModule::UnloadSteamModules()
{
#if LOADING_STEAM_LIBRARIES_DYNAMICALLY
	if (SteamDLLHandle != NULL)
	{
		FPlatformProcess::FreeDllHandle(SteamDLLHandle);
		SteamDLLHandle = NULL;
	}

	if (SteamServerDLLHandle != NULL)
	{
		FPlatformProcess::FreeDllHandle(SteamServerDLLHandle);
		SteamServerDLLHandle = NULL;
	}
#endif	//LOADING_STEAM_LIBRARIES_DYNAMICALLY
}

void FOnlineSubsystemSteamModule::StartupModule()
{
	bool bSuccess = false;

	// Load the Steam modules before first call to API
	LoadSteamModules();
	if (AreSteamDllsLoaded())
	{
		// Create and register our singleton factory with the main online subsystem for easy access
		SteamFactory = new FOnlineFactorySteam();

		FOnlineSubsystemModule& OSS = FModuleManager::GetModuleChecked<FOnlineSubsystemModule>("OnlineSubsystem");
		OSS.RegisterPlatformService(STEAM_SUBSYSTEM, SteamFactory);
		bSuccess = true;
	}
	else
	{
		UE_LOG_ONLINE(Warning, TEXT("Steam SDK %s libraries not present at %s or failed to load!"), STEAM_SDK_VER, *GetSteamModulePath());
	}

	if (!bSuccess)
	{
		UnloadSteamModules();
	}
}

void FOnlineSubsystemSteamModule::ShutdownModule()
{
	FOnlineSubsystemModule& OSS = FModuleManager::GetModuleChecked<FOnlineSubsystemModule>("OnlineSubsystem");
	OSS.UnregisterPlatformService(STEAM_SUBSYSTEM);
	
	delete SteamFactory;
	SteamFactory = NULL;

	UnloadSteamModules();
}
