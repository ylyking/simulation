#pragma once
#include "pch.h"

namespace Base
{
	interface ISubsystem
	{
		virtual bool Initialize() = 0;
		virtual void Update() = 0;
		virtual bool UnInitialize() = 0;
	};
}