#include "stdafx.h"

//
// Every function in this file is called from usermode because of
// SYSCALL/SYSENTER. Zw* functions never reach here.
//

volatile ULONG64 numCalls = 0;

NTSTATUS NTAPI hk_NtReadVirtualMemory(HANDLE ProcessHandle, PVOID BaseAddress, PVOID Buffer, SIZE_T NumberOfBytesToRead, PSIZE_T NumberOfBytesRead)
{
	numCalls++;

	//	if (numCalls % 1000 == 0)
	//		DbgLog("NtReadVirtualMemory - 0x%p 0x%p 0x%p 0x%p 0x%p\n", ProcessHandle, BaseAddress, Buffer, NumberOfBytesToRead, NumberOfBytesRead);

	return Nt::NtReadVirtualMemory(ProcessHandle, BaseAddress, Buffer, NumberOfBytesToRead, NumberOfBytesRead);
}

NTSTATUS NTAPI hk_NtClose(HANDLE Handle)
{
	PVOID object	= NULL;
	NTSTATUS status = ObReferenceObjectByHandle(Handle, 0, NULL, UserMode, &object, NULL);

	//
	// This will fail with an invalid handle
	//
	if (!NT_SUCCESS(status))
		return STATUS_INVALID_HANDLE;

	//
	// Continue execution normally
	//
	ObDereferenceObject(object);
	return Nt::NtClose(Handle);
}

NTSTATUS NTAPI hk_NtQuerySystemInformation(SYSTEM_INFORMATION_CLASS SystemInformationClass, PVOID SystemInformation, ULONG SystemInformationLength, PULONG ReturnLength)
{
	NTSTATUS status = Nt::NtQuerySystemInformation(SystemInformationClass, SystemInformation, SystemInformationLength, ReturnLength);

	//
	// Special case for SystemModuleInformation (11)
	// NOTE: Possible STATUS_INFO_LENGTH_MISMATCH (Short write)
	//
	if ((SystemInformationClass == SystemModuleInformation) &&
		(NT_SUCCESS(status) || status == STATUS_INFO_LENGTH_MISMATCH))
	{
		if (SystemInformation)
			RemoveDriverFromSysModuleInfo(SystemInformation, SystemInformationLength, ReturnLength);

		//
		// ALWAYS subtract one SYSTEM_MODULE from the return length
		// if it is not null and SystemInformation is null
		// (This driver counts as one module)
		//	
		if (!SystemInformation && ReturnLength && *ReturnLength >= sizeof(SYSTEM_MODULE))
			*ReturnLength -= sizeof(SYSTEM_MODULE);
	}

	//
	// Did the first call succeed?
	//
	if (!NT_SUCCESS(status))
		return status;

	//
	// It did, so now modify any return values
	//
	if (SystemInformation && SystemInformationLength > 0)
	{
		if (SystemInformationClass == SystemKernelDebuggerInformation)
		{
			PSYSTEM_KERNEL_DEBUGGER_INFORMATION debugInfo = (PSYSTEM_KERNEL_DEBUGGER_INFORMATION)SystemInformation;

			debugInfo->DebuggerEnabled		= FALSE;
			debugInfo->DebuggerNotPresent	= TRUE;
		}
		else if (SystemInformationClass == SystemKernelDebuggerInformationEx)
		{
			PSYSTEM_KERNEL_DEBUGGER_INFORMATION_EX debugInfoEx = (PSYSTEM_KERNEL_DEBUGGER_INFORMATION_EX)SystemInformation;

			debugInfoEx->BootedDebug		= FALSE;
			debugInfoEx->DebuggerEnabled	= FALSE;
			debugInfoEx->DebuggerPresent	= FALSE;
		}
	}

	return status;
}

NTSTATUS NTAPI hk_NtSetInformationThread(HANDLE ThreadHandle, THREADINFOCLASS ThreadInformationClass, PVOID ThreadInformation, ULONG ThreadInformationLength)
{
	//
	// Filter out ThreadHideFromDebugger
	//
	if (ThreadInformationClass == ThreadHideFromDebugger)
	{
		PKTHREAD object = NULL;
		NTSTATUS status = ObReferenceObjectByHandle(ThreadHandle, 0, *PsThreadType, ExGetPreviousMode(), (PVOID *)&object, NULL);
		
		if (NT_SUCCESS(status))
		{
			ObDereferenceObject(object);
			return STATUS_SUCCESS;
		}
	}

	return Nt::NtSetInformationThread(ThreadHandle, ThreadInformationClass, ThreadInformation, ThreadInformationLength);
}

NTSTATUS NTAPI hk_NtQueryObject(HANDLE Handle, OBJECT_INFORMATION_CLASS ObjectInformationClass, PVOID ObjectInformation, ULONG ObjectInformationLength, PULONG ReturnLength)
{
	NTSTATUS status = Nt::NtQueryObject(Handle, ObjectInformationClass, ObjectInformation, ObjectInformationLength, ReturnLength);

	//
	// Hide debug information queries
	// NOTE: Possible STATUS_INFO_LENGTH_MISMATCH (Short write)
	//
	if ((ObjectInformation) &&
		(NT_SUCCESS(status) || status == STATUS_INFO_LENGTH_MISMATCH))
	{
		if (ObjectInformationClass == ObjectTypeInformation)
		{
			//
			// Hide the single debug object info
			//
			if (ObjectInformationLength >= sizeof(OBJECT_TYPE_INFORMATION))
				RemoveSingleDebugObjectInfo((POBJECT_TYPE_INFORMATION)ObjectInformation);
		}
		else if (ObjectInformationClass == ObjectTypesInformation)
		{
			//
			// Loop all entries and fix the DebugObject entry
			//
			if (ObjectInformationLength > 0)
				RemoveDebugObjectInfo(ObjectInformation, ObjectInformationLength);
		}
	}

	return status;
}

NTSTATUS NTAPI hk_NtSystemDebugControl(DEBUG_CONTROL_CODE ControlCode, PVOID InputBuffer, ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength, PULONG ReturnLength)
{
	//return STATUS_DEBUGGER_INACTIVE;
	return Nt::NtSystemDebugControl(ControlCode, InputBuffer, InputBufferLength, OutputBuffer, OutputBufferLength, ReturnLength);
}