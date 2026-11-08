# game.c 地图（第 17 轮）

> 共 9101 行、122 个顶层函数、23 个 static 状态。
> 目的：改某一块时能直接跳过去，不必通读。

## 玩家 · 移动/手电（4 个）

| 函数 | 行号 |
|---|---|
| `PlayerInSmoke` | 769 |
| `UnstickPlayer` | 1172 |
| `FlashlightUseAllowed` | 3835 |
| `PlayerAtExit` | 3853 |

## 玩家 · 理智/体力（6 个）

| 函数 | 行号 |
|---|---|
| `BatteryMax` | 3068 |
| `BatteryDrainRate` | 3083 |
| `SanityDrainRate` | 3091 |
| `SanityMax` | 3288 |
| `StaminaMax` | 3294 |
| `EnergyMax` | 3305 |

## 输入（7 个）

| 函数 | 行号 |
|---|---|
| `ActionIsDown` | 534 |
| `PollInput` | 896 |
| `PauseGame` | 6781 |
| `ActivateMenuItem` | 6920 |
| `ActivateHubRow` | 7369 |
| `ActivateSettingRow` | 7403 |
| `MouseToLogical` | 7494 |

## 世界 · 关卡（2 个）

| 函数 | 行号 |
|---|---|
| `GenerateLevel` | 1958 |
| `MapCursorRefresh` | 8606 |

## 实体 · 怪物（7 个）

| 函数 | 行号 |
|---|---|
| `SpawnSmoke` | 593 |
| `SpawnGlue` | 673 |
| `MimicSpawnShell` | 732 |
| `MonsterCollect` | 747 |
| `MonsterStunAll` | 757 |
| `MonsterThreatScore` | 3267 |
| `SpawnShards` | 3383 |

## 进度 · 存档（3 个）

| 函数 | 行号 |
|---|---|
| `BankPreviewGain` | 3154 |
| `BankSettle` | 3161 |
| `UnlockAchievement` | 6691 |

## 道具 · 投掷（5 个）

| 函数 | 行号 |
|---|---|
| `ShotBlocked` | 1229 |
| `LockboxPctForFloor` | 1801 |
| `EchoMarksReset` | 3443 |
| `EchoMarksScan` | 3453 |
| `EchoMarksTick` | 3536 |

## 其它（88 个）

| 函数 | 行号 |
|---|---|
| `EndlessUnlocked` | 293 |
| `AddShake` | 337 |
| `AddHitFlash` | 341 |
| `EventScore` | 384 |
| `itemHasStock` | 549 |
| `SelectVisibleSlot` | 560 |
| `EnsureThrowSelectedValid` | 573 |
| `PlaceDecoy` | 614 |
| `ArmDecoy` | 635 |
| `DecoyIntercept` | 650 |
| `PointInSmoke` | 693 |
| `SegmentInSmoke` | 704 |
| `UpdateSmokes` | 778 |
| `UpdateDynamicRes` | 855 |
| `IsActionPressed` | 965 |
| `BindActionOfItem` | 989 |
| `IsKeyBindItem` | 995 |
| `SettingsRegister` | 1043 |
| `PageItems` | 1142 |
| `SettingsRowCount` | 1151 |
| `Blocked` | 1188 |
| `SpotClear` | 1234 |
| `RollLevelMod` | 1260 |
| `ContainerTargetCount` | 1280 |
| `BuildTrainingArena` | 1300 |
| `TutorialStepTotal` | 1461 |
| `TutorialStepNumber` | 1462 |
| `TutorialBegin` | 1544 |
| `TutorialAdvance` | 1561 |
| `TutorialUpdate` | 1583 |
| `FindFarthestFromCell` | 1696 |
| `FindFarthestCell` | 1745 |
| `ApplyLockboxes` | 1808 |
| `BraidDeadEnds` | 1907 |
| `ThemeRoomBudgetForLevel` | 1951 |
| `ForceCursor` | 2994 |
| `CanityMouseLook` | 3004 |
| `UpdateExplored` | 3035 |
| `SafeHealRate` | 3086 |
| `BottleCap` | 3100 |
| `GoldSearchAt` | 3233 |
| `ExtractDepthMul` | 3243 |
| `EndlessDepthBonus` | 3282 |
| `UpgradeLevel` | 3307 |
| `UpgradeCost` | 3330 |
| `chargeBonus` | 3340 |
| `WeaponDamageType` | 3359 |
| `WrenchSwingProgress` | 3585 |
| `WrenchSwingFrame` | 3591 |
| `WrenchApplyHit` | 3601 |
| `WrenchStartSwing` | 3680 |
| `WrenchSwingStartForTest` | 3741 |
| `WrenchTick` | 3752 |
| `WrenchKnockUpdate` | 3796 |
| `UpdateTrainingPad` | 3859 |
| `UpdateSmokeBlindness` | 3884 |
| `Update` | 3911 |
| `StartDailyRun` | 6099 |
| `StartCampaign` | 6118 |
| `SkipOneLevel` | 6132 |
| `CalibZoneScale` | 6165 |
| `CalibSpeedScale` | 6174 |
| `CalibStart` | 6186 |
| `CalibLock` | 6233 |
| `CalibTick` | 6316 |
| `SyncWindowSize` | 6353 |
| `ResetModeFlags` | 6373 |
| `ResetRunModeFlags` | 6393 |
| `ApplyCampaignRules` | 6402 |
| `StartGame` | 6419 |
| `CheckAchievements` | 6699 |
| `ApplyDisplayMode` | 6812 |
| `ApplySetting` | 6900 |
| `RewardScheduleForLevel` | 7113 |
| `CardUsableNow` | 7145 |
| `OpenSkillPick` | 7151 |
| `OpenRelicPick` | 7250 |
| `ApplyInstantSkill` | 7273 |
| `CloseSkillPick` | 7339 |
| `HitTestMenu` | 7513 |
| `ClickMenu` | 7615 |
| `CanityMenuAction` | 7813 |
| `CanityPauseToggle` | 7858 |
| `CanityToggleFlashlight` | 7869 |
| `CanityMouseWheel` | 7875 |
| `CanityMouseMove` | 7930 |
| `CanityMouseClick` | 7940 |
| `CanityKeyDown` | 8021 |

## static 状态（23 个）

| 名字 | 行号 |
|---|---|
| `tutorialTimer` | 190 |
| `tutorialActionTimer` | 191 |
| `tutorialStepTimer` | 204 |
| `tutorialStartX` | 205 |
| `tutorialStartY` | 205 |
| `tutorialStartDirX` | 206 |
| `tutorialStartDirY` | 206 |
| `tutorialLightOff` | 207 |
| `tutorialThrown` | 208 |
| `tutorialKnocked` | 209 |
| `tutorialMapOpened` | 210 |
| `s_decoyKeyCd` | 376 |
| `pendingSkillDraft` | 402 |
| `pendingStartMode` | 439 |
| `START_NORMAL` | 439 |
| `pendingCampaignStage` | 440 |
| `mapHeld` | 984 |
| `calibElapsed` | 3142 |
| `calibStartLatch` | 3143 |
| `wrenchHitMask` | 3600 |
| `wrenchPrev` | 3782 |
| `wrenchInit` | 3783 |
| `wrenchArmed` | 3794 |
