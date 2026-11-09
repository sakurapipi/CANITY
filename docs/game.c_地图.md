# game.c 与循环模块导航（第三期）

日期：2026-11-09。行号来自当前本地源码；不代表编译或运行通过。

## src/game.c（5562 行）

| 函数 | 起始行 | 函数行数 |
|---|---:|---:|
| `campaignName` | 317 | 5 |
| `IsMouseButton` | 348 | 3 |
| `itemHasStock` | 366 | 6 |
| `SelectVisibleSlot` | 377 | 12 |
| `EnsureThrowSelectedValid` | 390 | 18 |
| `MimicSpawnShell` | 453 | 14 |
| `MonsterCollect` | 469 | 8 |
| `MonsterStunAll` | 481 | 10 |
| `UpdateDynamicRes` | 496 | 40 |
| `ForceCursor` | 607 | 3 |
| `CanityMouseLook` | 617 | 30 |
| `UpdateExplored` | 647 | 33 |
| `BankPreviewGain` | 711 | 7 |
| `BankSettle` | 720 | 48 |
| `GoldSearchAt` | 791 | 6 |
| `MonsterThreatScore` | 821 | 11 |
| `chargeBonus` | 837 | 5 |
| `WeaponDamageType` | 854 | 12 |
| `EchoMarksReset` | 916 | 8 |
| `EchoMarksScan` | 928 | 80 |
| `EchoMarksTick` | 1012 | 10 |
| `FlashlightUseAllowed` | 1061 | 14 |
| `UpdateTrainingPad` | 1082 | 22 |
| `UpdateSmokeBlindness` | 1107 | 26 |
| `UpdatePreflight` | 1135 | 89 |
| `UpdatePlayableTimers` | 1225 | 58 |
| `UpdateSurvival` | 1284 | 40 |
| `UpdateCoverInput` | 1325 | 161 |
| `UpdateEarlyActions` | 1487 | 153 |
| `UpdateRestAndShop` | 1641 | 89 |
| `UpdateLegacyEffects` | 1731 | 72 |
| `UpdatePhaseAndCrank` | 1804 | 145 |
| `UpdateMovementAndHatches` | 1950 | 31 |
| `UpdateHeldInteraction` | 1982 | 250 |
| `UpdateThrowInput` | 2233 | 160 |
| `UpdateThreatsAndAudio` | 2394 | 216 |
| `UpdateWorldParticles` | 2611 | 92 |
| `UpdateExitProgression` | 2704 | 257 |
| `Update` | 2962 | 21 |
| `StartDailyRun` | 2990 | 17 |
| `StartCampaign` | 3009 | 11 |
| `SkipOneLevel` | 3023 | 11 |
| `SyncWindowSize` | 3052 | 15 |
| `ResetModeFlags` | 3071 | 12 |
| `ResetRunModeFlags` | 3087 | 8 |
| `StartGame` | 3096 | 263 |
| `UnlockAchievement` | 3363 | 1 |
| `CheckAchievements` | 3371 | 80 |
| `PauseGame` | 3453 | 30 |
| `ApplyDisplayMode` | 3484 | 87 |
| `ActivateMenuItem` | 3573 | 181 |
| `ActivateHubRow` | 3768 | 32 |
| `MouseToLogical` | 3820 | 7 |
| `HitTestMenu` | 3839 | 101 |
| `ClickMenu` | 3941 | 160 |
| `CanityMenuAction` | 4139 | 44 |
| `CanityPauseToggle` | 4184 | 10 |
| `CanityToggleFlashlight` | 4195 | 5 |
| `CanityMouseWheel` | 4201 | 44 |
| `CanityMouseMove` | 4256 | 9 |
| `CanityMouseClick` | 4266 | 65 |
| `MenuKeyRead` | 4351 | 29 |
| `KeyHandleMainMenu` | 4381 | 35 |
| `KeyHandlePaused` | 4417 | 18 |
| `KeyHandleSettings` | 4436 | 62 |
| `KeyHandleGameOver` | 4499 | 6 |
| `KeyHandleHelp` | 4506 | 30 |
| `KeyHandleSkillPick` | 4537 | 14 |
| `KeyHandleSafehouse` | 4552 | 71 |
| `KeyHandleHub` | 4624 | 60 |
| `KeyHandleBank` | 4685 | 30 |
| `KeyHandleBestiary` | 4716 | 20 |
| `KeyHandleAchievements` | 4737 | 23 |
| `KeyHandlePlaying` | 4761 | 116 |
| `KeyHandleGlobalShortcut` | 4878 | 60 |
| `KeyHandleLock` | 4939 | 19 |
| `KeyPrepareInput` | 4959 | 82 |
| `CanityKeyDown` | 5042 | 23 |
| `MapCursorRefresh` | 5071 | 7 |
| `WindowProc` | 5079 | 474 |

## src/loop_calibration.c（249 行）

| 函数 | 起始行 | 函数行数 |
|---|---:|---:|
| `CalibZoneScale` | 40 | 7 |
| `CalibSpeedScale` | 49 | 11 |
| `CalibStart` | 61 | 46 |
| `CalibLock` | 108 | 82 |
| `CalibTick` | 191 | 36 |
| `CalibHandleInput` | 228 | 15 |
| `CalibResetRun` | 244 | 6 |

## src/loop_input.c（177 行）

| 函数 | 起始行 | 函数行数 |
|---|---:|---:|
| `ClearKeyEdges` | 10 | 3 |
| `IsKeyBindItem` | 14 | 3 |
| `IsActionDown` | 63 | 8 |
| `ActionIsDown` | 72 | 1 |
| `PollInput` | 74 | 68 |
| `IsActionPressed` | 143 | 9 |
| `VkName` | 153 | 19 |
| `BindActionOfItem` | 173 | 5 |

## src/loop_interaction.c（78 行）

| 函数 | 起始行 | 函数行数 |
|---|---:|---:|
| `InteractionRefreshTargets` | 11 | 68 |

## src/loop_items.c（215 行）

| 函数 | 起始行 | 函数行数 |
|---|---:|---:|
| `SpawnSmoke` | 30 | 9 |
| `PlaceDecoy` | 40 | 20 |
| `ArmDecoy` | 61 | 13 |
| `DecoyIntercept` | 75 | 19 |
| `SpawnGlue` | 95 | 16 |
| `PointInSmoke` | 112 | 8 |
| `SegmentInSmoke` | 121 | 9 |
| `PlayerInSmoke` | 131 | 8 |
| `UpdateSmokes` | 140 | 76 |

## src/loop_level.c（1427 行）

| 函数 | 起始行 | 函数行数 |
|---|---:|---:|
| `RollLevelMod` | 52 | 17 |
| `ContainerTargetCount` | 70 | 8 |
| `FindFarthestFromCell` | 79 | 35 |
| `FindFarthestCell` | 128 | 22 |
| `LockboxPctForFloor` | 186 | 6 |
| `ApplyLockboxes` | 193 | 56 |
| `BraidDeadEnds` | 292 | 43 |
| `ThemeRoomBudgetForLevel` | 336 | 6 |
| `ApplyCampaignRules` | 343 | 16 |
| `ResetFloorSystems` | 360 | 59 |
| `GenerateMazeTopology` | 420 | 143 |
| `CollectFloorCandidates` | 564 | 109 |
| `ConfigureFloorResources` | 674 | 166 |
| `PlaceFloorContainers` | 841 | 182 |
| `DistributeFloorLoot` | 1024 | 104 |
| `SpawnFloorPopulation` | 1129 | 203 |
| `PlaceFloorBreaker` | 1333 | 79 |
| `GenerateLevel` | 1413 | 15 |

## src/loop_particles.c（43 行）

| 函数 | 起始行 | 函数行数 |
|---|---:|---:|
| `SpawnParticles` | 14 | 5 |
| `SpawnShards` | 24 | 20 |

## src/loop_player.c（71 行）

| 函数 | 起始行 | 函数行数 |
|---|---:|---:|
| `BatteryDrainRate` | 16 | 1 |
| `EnergyMax` | 18 | 1 |
| `SanityMax` | 20 | 5 |
| `SafeHealRate` | 26 | 3 |
| `BatteryMax` | 31 | 13 |
| `SanityDrainRate` | 45 | 7 |
| `BottleCap` | 53 | 9 |
| `StaminaMax` | 63 | 9 |

## src/loop_player_tick.c（355 行）

| 函数 | 起始行 | 函数行数 |
|---|---:|---:|
| `PlayerTickVitals` | 36 | 92 |
| `PlayerTickBattery` | 129 | 15 |
| `PlayerPrepareMovement` | 145 | 114 |
| `PlayerApplyMovement` | 260 | 96 |

## src/loop_present.c（36 行）

| 函数 | 起始行 | 函数行数 |
|---|---:|---:|
| `AddHitFlash` | 9 | 4 |
| `AddShake` | 17 | 4 |
| `PresentTickFeedback` | 22 | 15 |

## src/loop_rewards.c（279 行）

| 函数 | 起始行 | 函数行数 |
|---|---:|---:|
| `RewardScheduleForLevel` | 23 | 17 |
| `CardUsableNow` | 55 | 4 |
| `OpenSkillPick` | 61 | 93 |
| `OpenRelicPick` | 160 | 22 |
| `ApplyInstantSkill` | 183 | 65 |
| `CloseSkillPick` | 249 | 29 |
| `RewardResetPending` | 279 | 1 |

## src/loop_rng.c（28 行）

| 函数 | 起始行 | 函数行数 |
|---|---:|---:|
| `RunRngNext` | 15 | 7 |
| `RunRngSeed` | 23 | 3 |
| `RunRngState` | 27 | 1 |
| `RunRngSetState` | 28 | 1 |

## src/loop_settings.c（276 行）

| 函数 | 起始行 | 函数行数 |
|---|---:|---:|
| `SettingsRegister` | 72 | 7 |
| `PageTitle` | 152 | 11 |
| `PageItems` | 171 | 8 |
| `ApplySetting` | 180 | 19 |
| `ActivateSettingRow` | 200 | 71 |
| `SettingsRowCount` | 272 | 5 |

## src/loop_tutorial.c（450 行）

| 函数 | 起始行 | 函数行数 |
|---|---:|---:|
| `TutorialStepTotal` | 32 | 1 |
| `TutorialStepNumber` | 34 | 4 |
| `TutorialBegin` | 39 | 16 |
| `TrainingBuildArena` | 57 | 154 |
| `TutorialObjective` | 217 | 57 |
| `TutorialHint` | 275 | 18 |
| `TutorialAdvance` | 295 | 21 |
| `TutorialUpdate` | 317 | 103 |
| `TutorialResetMode` | 421 | 7 |
| `TutorialRestartGuide` | 429 | 5 |
| `TutorialRecordThrow` | 435 | 1 |
| `TutorialRecordNoise` | 436 | 1 |
| `TutorialRecordMapOpened` | 437 | 3 |
| `TutorialVisitStation` | 441 | 10 |

## src/loop_world.c（185 行）

| 函数 | 起始行 | 函数行数 |
|---|---:|---:|
| `GoldBreakerAt` | 48 | 1 |
| `GoldExitAt` | 49 | 1 |
| `PlayerAtExit` | 51 | 3 |
| `EndlessDepthBonus` | 55 | 5 |
| `ShotBlocked` | 61 | 5 |
| `ExtractDepthMul` | 67 | 6 |
| `UpgradeCost` | 74 | 5 |
| `EndlessUnlocked` | 81 | 3 |
| `EventScore` | 85 | 3 |
| `UpgradeLevel` | 89 | 10 |
| `UpgradeName` | 100 | 10 |
| `UnstickPlayer` | 114 | 14 |
| `Blocked` | 129 | 36 |
| `SpotClear` | 166 | 20 |

## src/loop_wrench.c（270 行）

| 函数 | 起始行 | 函数行数 |
|---|---:|---:|
| `WrenchSwingProgress` | 29 | 4 |
| `WrenchSwingFrame` | 35 | 7 |
| `WrenchApplyHit` | 45 | 77 |
| `WrenchStartSwing` | 124 | 53 |
| `WrenchSwingStartForTest` | 185 | 9 |
| `WrenchTick` | 196 | 17 |
| `WrenchKnockUpdate` | 236 | 35 |

## HUD 单元

| 函数 | 起始行 | 函数行数 |
|---|---:|---:|
| `DrawHudThemeRoom` | 1779 | 24 |
| `HudBuildBarLayout` | 1808 | 17 |
| `DrawHudRunHeader` | 1826 | 236 |
| `DrawHudVitalsAndStatus` | 2063 | 224 |
| `DrawHudInventory` | 2288 | 125 |
| `DrawHudWorldHints` | 2414 | 70 |
| `DrawHudSafeHints` | 2485 | 74 |
| `DrawHudCoverAndThreat` | 2560 | 172 |
| `DrawHudLightBar` | 2733 | 34 |
| `DrawHudPerception` | 2768 | 69 |
| `DrawHudActionHints` | 2838 | 326 |
| `DrawHUD` | 3165 | 15 |

## 本轮新增的绘制 / AI 私有单元

### src/sprites.c

| 函数 | 起始行 | 函数行数 |
|---|---:|---:|
| `BakeMonsterEcho` | 892 | 40 |
| `BakeMonsterMimic` | 934 | 85 |
| `BakeMonsterHumanoid` | 1021 | 411 |
| `BakeMonsterMite` | 1434 | 28 |
| `BakeMonsterJumper` | 1464 | 29 |
| `BakeMonsterShade` | 1495 | 26 |
| `BakeMonsterResonator` | 1523 | 35 |
| `BakeMonster` | 1559 | 105 |
| `BakePropCrate` | 1811 | 78 |
| `BakePropBeaconGround` | 1891 | 65 |
| `BakePropSpider` | 1958 | 71 |
| `BakePropBeaconHand` | 2031 | 42 |
| `BakePropBreaker` | 2075 | 57 |
| `BakePropExit` | 2134 | 14 |
| `BakePropOpenCrate` | 2150 | 22 |
| `BakePropLockedCrate` | 2174 | 27 |
| `BakePropMimicCrate` | 2203 | 45 |
| `BakePropTrash` | 2250 | 30 |
| `BakePropSceneFixture` | 2282 | 126 |
| `BakeProp` | 2409 | 31 |

### src/entity.c

| 函数 | 起始行 | 函数行数 |
|---|---:|---:|
| `HunterActiveCount` | 765 | 6 |
| `HunterIndex` | 784 | 5 |
| `HunterHearMulAt` | 790 | 4 |
| `HunterHearMul` | 796 | 9 |
| `HunterPerceive` | 993 | 186 |
| `HunterUpdateState` | 1180 | 121 |
| `HunterFollowPath` | 1302 | 20 |
| `HunterResolveContact` | 1323 | 123 |
| `HunterUpdateFacing` | 1447 | 37 |
| `UpdateOneHunter` | 1485 | 7 |

### src/warden.c

| 函数 | 起始行 | 函数行数 |
|---|---:|---:|
| `WardenPreflight` | 623 | 54 |
| `WardenTickState` | 678 | 141 |
| `WardenObservePlayer` | 820 | 150 |
| `WardenSteerToTarget` | 971 | 73 |
| `WardenPatrol` | 1045 | 95 |
| `UpdateWarden` | 1141 | 7 |


后续整改：本文的行号和规模数据为上一轮快照；当前改造树接口退役与诱饵绘制拆分见 [质量整改第二轮交接](质量整改_第二轮交接.md)。本轮尚未运行验证。


后续源码整理见 [质量整改第三轮交接](质量整改_第三轮交接.md)：查询接口分类、烘焙/精灵分阶段及六个临时脚本清理。本轮验证仍由用户负责。


后续目标整改见 [第四轮交接](目标整改_第四轮交接.md)：对象宏移除、循环私有状态归属、阶段拆分、桩分类和验收工具源码；验证与提交仍由用户负责。
