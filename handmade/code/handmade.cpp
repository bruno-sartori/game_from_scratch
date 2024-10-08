#include "handmade.h"
#include "handmade_render_group.cpp"
#include "handmade_world.cpp"
#include "handmade_random.h"
#include "handmade_sim_region.cpp"
#include "handmade_entity.cpp"

internal void GameOutputSound(game_state *GameState, game_sound_output_buffer *SoundBuffer, int ToneHz) {
  int16 ToneVolume = 3000;
  int WavePeriod = SoundBuffer->SamplesPerSecond / ToneHz;

  int16 *SampleOut = SoundBuffer->Samples;
  for (int SampleIndex = 0; SampleIndex < SoundBuffer->SampleCount; ++SampleIndex) {
    // TODO: Draw this out for people
#if 0
    real32 SineValue = sinf(GameState->tSine);
    int16 SampleValue = (int16)(SineValue * ToneVolume);
#else
    int16 SampleValue = 0;
#endif
    *SampleOut++ = SampleValue;
    *SampleOut++ = SampleValue;

#if 0
    GameState->tSine += 2.0f*Pi32*1.0f/(real32)WavePeriod;
    if(GameState->tSine > 2.0f*Pi32) {
      GameState->tSine -= 2.0f*Pi32;
    }
#endif
  }
}

// NOTE: Pragma pack(operation, byte) tells the compiler to align struct properties in desired bytes so that it does not
// changes the bytes size structure when dealing with different size properties and then we can use the struct to properly
// cast the file contents to it
#pragma pack(push, 1)
struct bitmap_header {
  uint16 FileType;
  uint32 FileSize;
  uint16 Reserved1;
  uint16 Reserved2;
  uint32 BitmapOffset;
  uint32 Size;
  int32 Width;
  int32 Height;
  uint16 Planes;
  uint16 BitsPerPixel;
  uint32 Compression;
  uint32 SizeOfBitmap;
  int32 HorzResolution;
  int32 VertResolution;
  uint32 ColorsUsed;
  uint32 ColorsImportant;

  uint32 RedMask;
  uint32 GreenMask;
  uint32 BlueMask;
};
#pragma pack(pop)

inline v2 TopDownAlign(loaded_bitmap *Bitmap, v2 Align) {
  Align.y = (real32)(Bitmap->Height - 1) - Align.y;
  return Align;
}

internal loaded_bitmap DEBUGLoadBMP(thread_context *Thread, debug_platform_read_entire_file *ReadEntireFile, char* FileName, int32 AlignX = 0, int32 TopDownAlignY = 0) {
  loaded_bitmap Result = {};

  debug_read_file_result ReadResult = ReadEntireFile(Thread, FileName);
  if (ReadResult.ContentsSize != 0) {
    bitmap_header *Header = (bitmap_header *)ReadResult.Contents;
    uint32 *Pixels = (uint32 *)((uint8 *)ReadResult.Contents + Header->BitmapOffset);
    Result.Memory = Pixels;
    Result.Width = Header->Width;
    Result.Height = Header->Height;
    Result.Align = TopDownAlign(&Result, V2i(AlignX, TopDownAlignY));

    Assert(Result.Height >= 0);
    Assert(Header->Compression == 3);

    // NOTE: If you are using this generically for some reason,
    // please remember that BMP files CAN GO IN EITHER DIRECTION and
    // the height will be negative for top-down.
    // Alse, there can be compression, etc. DONT think this is complete
    // BMP loading code because it isnt!!!


    // NOTE: Byte order in memory is determined by the Header itself.
    // So we have to read out the masks and convert the pixels ourselves.
    uint32 RedMask = Header->RedMask;
    uint32 GreenMask = Header->GreenMask;
    uint32 BlueMask = Header->BlueMask;
    uint32 AlphaMask = ~(RedMask | GreenMask | BlueMask);

    bit_scan_result RedScan = FindLeastSignificantSetBit(RedMask);
    bit_scan_result GreenScan = FindLeastSignificantSetBit(GreenMask);
    bit_scan_result BlueScan = FindLeastSignificantSetBit(BlueMask);
    bit_scan_result AlphaScan = FindLeastSignificantSetBit(AlphaMask);

    Assert(RedScan.Found);
    Assert(GreenScan.Found);
    Assert(BlueScan.Found);
    Assert(AlphaScan.Found);


    int32 RedShiftDown = (int32)RedScan.Index;
    int32 GreenShiftDown = (int32)GreenScan.Index;
    int32 BlueShiftDown = (int32)BlueScan.Index;
    int32 AlphaShiftDown = (int32)AlphaScan.Index;

    uint32 *SourceDest = Pixels;
    for (int32 Y = 0; Y < Header->Height; ++Y) {
      for (int32 X = 0; X < Header->Width; ++X) {
        uint32 C = *SourceDest;

        v4 Texel = {
          (real32)((C & RedMask) >> RedShiftDown),
          (real32)((C & GreenMask) >> GreenShiftDown),
          (real32)((C & BlueMask) >> BlueShiftDown),
          (real32)((C & AlphaMask) >> AlphaShiftDown)
        };

        Texel = SRGB255ToLinear1(Texel);
#if 1
        // NOTE: Premultiplied Alpha
        Texel.rgb *= Texel.a;
#endif
        Texel = Linear1ToSRGB255(Texel);

        *SourceDest++ = (
          ((uint32)(Texel.a + 0.5f) << 24) |
          ((uint32)(Texel.r + 0.5f) << 16) |
          ((uint32)(Texel.g + 0.5f) << 8) |
          ((uint32)(Texel.b + 0.5f) << 0)
        );
      }
    }
  }

  Result.Pitch = Result.Width * BITMAP_BITES_PER_PIXEL;

#if 0
  Result.Memory = (uint8 *)Result.Memory + Result.Pitch * (Result.Height - 1);
  Result.Pitch = -Result.Pitch;
#endif

  return Result;
}

struct add_low_entity_result {
  uint32 LowIndex;
  low_entity *Low;
};

internal add_low_entity_result AddLowEntity(game_state *GameState, entity_type Type, world_position P) {
  Assert(GameState->LowEntityCount < ArrayCount(GameState->LowEntities));
  uint32 EntityIndex = GameState->LowEntityCount++;

  low_entity *EntityLow = GameState->LowEntities + EntityIndex;
  *EntityLow = {};
  EntityLow->Sim.Type = Type;
  EntityLow->Sim.Collision = GameState->NullCollision;
  EntityLow->P = NullPosition();

  ChangeEntityLocation(&GameState->WorldArena, GameState->World, EntityIndex, EntityLow, P);

  add_low_entity_result Result;
  Result.Low = EntityLow;
  Result.LowIndex = EntityIndex;

  // TODO: Do we need to have a begin/end paradigm for adding
  // entities so that they can be brought into the high set when they
  // are added and are in the camera region?

  return Result;
}


internal add_low_entity_result AddGroundedEntity(game_state *GameState, entity_type Type, world_position P, sim_entity_collision_volume_group *Collision) {
  add_low_entity_result Entity = AddLowEntity(GameState, Type, P);
  Entity.Low->Sim.Collision = Collision;
  return Entity;
}

inline world_position ChunkPositionFromTilePosition(world *World, int32 AbsTileX, int32 AbsTileY, int32 AbsTileZ, v3 AdditionalOffset = V3(0, 0, 0)) {
  world_position BasePos = {};

  real32 TileSideInMeters = 1.4f;
  real32 TileDepthInMeters = 3.0f;

  v3 TileDim = V3(TileSideInMeters, TileSideInMeters, TileDepthInMeters);
  v3 Offset = Hadamard(TileDim, V3((real32)AbsTileX, (real32)AbsTileY, (real32)AbsTileZ));
  world_position Result = MapIntoChunkSpace(World, BasePos, AdditionalOffset + Offset);

  Assert(IsCanonical(World, Result.Offset));

  return Result;
}

internal add_low_entity_result AddStandardRoom(game_state *GameState, uint32 AbsTileX, uint32 AbsTileY, uint32 AbsTileZ) {
  world_position P = ChunkPositionFromTilePosition(GameState->World, AbsTileX, AbsTileY, AbsTileZ);
  add_low_entity_result Entity = AddGroundedEntity(GameState, EntityType_Space, P, GameState->StandardRoomCollision);
  AddFlags(&Entity.Low->Sim, EntityFlag_Traversable);

  return Entity;
}

internal add_low_entity_result AddWall(game_state *GameState, uint32 AbsTileX, uint32 AbsTileY, uint32 AbsTileZ) {
  world_position P = ChunkPositionFromTilePosition(GameState->World, AbsTileX, AbsTileY, AbsTileZ);
  add_low_entity_result Entity = AddGroundedEntity(GameState, EntityType_Wall, P, GameState->WallCollision);
  AddFlags(&Entity.Low->Sim, EntityFlag_Collides);

  return Entity;
}

internal add_low_entity_result AddStair(game_state *GameState, uint32 AbsTileX, uint32 AbsTileY, uint32 AbsTileZ) {
  world_position P = ChunkPositionFromTilePosition(GameState->World, AbsTileX, AbsTileY, AbsTileZ);
  add_low_entity_result Entity = AddGroundedEntity(GameState, EntityType_Stairwell, P, GameState->StairCollision);
  AddFlags(&Entity.Low->Sim, EntityFlag_Collides);
  Entity.Low->Sim.WalkableDim = Entity.Low->Sim.Collision->TotalVolume.Dim.xy;
  Entity.Low->Sim.WalkableHeight = GameState->TypicalFloorHeight;


  return Entity;
}

internal void InitHitPoints(low_entity *EntityLow, uint32 HitPointCount) {
  Assert(HitPointCount <= ArrayCount(EntityLow->Sim.HitPoint));
  EntityLow->Sim.HitPointMax = HitPointCount;

  for (uint32 HitPointIndex = 0; HitPointIndex < EntityLow->Sim.HitPointMax; ++HitPointIndex) {
    hit_point *HitPoint = EntityLow->Sim.HitPoint + HitPointIndex;
    HitPoint->Flags = 0;
    HitPoint->FilledAmount = HIT_POINT_SUB_COUNT;
  }
}

internal add_low_entity_result AddSword(game_state *GameState) {
  add_low_entity_result Entity = AddLowEntity(GameState, EntityType_Sword, NullPosition());
  Entity.Low->Sim.Collision = GameState->SwordCollision;
  AddFlags(&Entity.Low->Sim, EntityFlag_Moveable);

  return Entity;
}

internal add_low_entity_result AddPlayer(game_state *GameState) {
  world_position P = GameState->CameraP;
  add_low_entity_result Entity = AddGroundedEntity(GameState, EntityType_Hero, P, GameState->PlayerCollision);
  AddFlags(&Entity.Low->Sim, EntityFlag_Collides|EntityFlag_Moveable);

  InitHitPoints(Entity.Low, 3);

  add_low_entity_result Sword = AddSword(GameState);
  Entity.Low->Sim.Sword.Index = Sword.LowIndex;

  if (GameState->CameraFollowingEntityIndex == 0) {
    GameState->CameraFollowingEntityIndex = Entity.LowIndex;
  }

  return Entity;
}

internal add_low_entity_result AddMonster(game_state *GameState, uint32 AbsTileX, uint32 AbsTileY, uint32 AbsTileZ) {
  world_position P = ChunkPositionFromTilePosition(GameState->World, AbsTileX, AbsTileY, AbsTileZ);
  add_low_entity_result Entity = AddGroundedEntity(GameState, EntityType_Monster, P, GameState->MonsterCollision);
  AddFlags(&Entity.Low->Sim, EntityFlag_Collides|EntityFlag_Moveable);

  InitHitPoints(Entity.Low, 3);

  return Entity;
}

internal add_low_entity_result AddFamiliar(game_state *GameState, uint32 AbsTileX, uint32 AbsTileY, uint32 AbsTileZ) {
  world_position P = ChunkPositionFromTilePosition(GameState->World, AbsTileX, AbsTileY, AbsTileZ);
  add_low_entity_result Entity = AddGroundedEntity(GameState, EntityType_Familiar, P, GameState->FamiliarCollision);

  AddFlags(&Entity.Low->Sim, EntityFlag_Collides|EntityFlag_Moveable);

  return Entity;
}

inline void DrawHitpoints(sim_entity *Entity, render_group *RenderGroup) {
  if (Entity->HitPointMax >= 1) {
    v2 HealthDim = { 0.2f, 0.2f };
    real32 SpacingX = 1.5f * HealthDim.x;
    v2 HitP = { -0.5f * (Entity->HitPointMax - 1) * SpacingX, -0.25f };
    v2 dHitP = { SpacingX, 0.0f };
    for(uint32 HealthIndex = 0; HealthIndex < Entity->HitPointMax; ++HealthIndex) {
      hit_point *HitPoint = Entity->HitPoint + HealthIndex;
      v4 Color = { 1.0f, 0.0f, 0.0f, 1.0f };

      if (HitPoint->FilledAmount == 0) {
        Color = V4(0.2f, 0.2f, 0.2f, 1.0f);
      }

      PushRect(RenderGroup, V3(HitP, 0), HealthDim, Color);
      HitP += dHitP;
    }
  }
}

internal void ClearCollisionRulesFor(game_state *GameState, uint32 StorageIndex) {
  // NOTE: Searches for the collision rule in the linked list, if the
  // rule that the actual rule is pointing to is a match, it points
  // to where the match rule was pointing to, to skip the match rule
  // from the list as we already found that it was a match

  // TODO: Need to make a better data structure that allows
  // removal of collision rules without searching the entire table

  // NOTE: One way to make removal easy would be to always
  // add _both_ orders of the pairs of storage indices to the
  // hash table, so no matter which position the entity is in,
  // you can always find it. Then, when you do your first pass
  // through for removal, you just remember the original top
  // of the free list, and when you`re done, do a pass through all
  // the new things on the free list, and remove the reverse of
  // those pairs.
  for(uint32 HashBucket = 0; HashBucket < ArrayCount(GameState->CollisionRuleHash); ++HashBucket) {
    for(pairwise_collision_rule **Rule = &GameState->CollisionRuleHash[HashBucket]; *Rule; ) {
      if (((*Rule)->StorageIndexA == StorageIndex) || ((*Rule)->StorageIndexB == StorageIndex)) {
        pairwise_collision_rule *RemovedRule = *Rule;
        *Rule = (*Rule)->NextInHash;

        RemovedRule->NextInHash = GameState->FirstFreeCollisionRule;
        GameState->FirstFreeCollisionRule = RemovedRule;
      } else {
        Rule = &(*Rule)->NextInHash;
      }
    }
  }
}

internal void AddCollisionRule(game_state *GameState, uint32 StorageIndexA, uint32 StorageIndexB, bool32 CanCollide) {
  // TODO: Collapse this with ShouldCollide
   if (StorageIndexA > StorageIndexB) {
    uint32 Temp = StorageIndexA;
    StorageIndexA = StorageIndexB;
    StorageIndexB = Temp;
  }


  // TODO: BETTER HASH FUNCTION
  pairwise_collision_rule *Found = 0;
  uint32 HashBucket = StorageIndexA & (ArrayCount(GameState->CollisionRuleHash) - 1);
  for(pairwise_collision_rule *Rule = GameState->CollisionRuleHash[HashBucket]; Rule; Rule = Rule->NextInHash) {
    if ((Rule->StorageIndexA == StorageIndexA) && (Rule->StorageIndexB == StorageIndexB)) {
      Found = Rule;
      break;
    }
  }

  if (!Found) {
    Found = GameState->FirstFreeCollisionRule;

    if (Found) {
      GameState->FirstFreeCollisionRule = Found->NextInHash;
    } else {
      Found = PushStruct(&GameState->WorldArena, pairwise_collision_rule);
    }

    Found->NextInHash = GameState->CollisionRuleHash[HashBucket];
    GameState->CollisionRuleHash[HashBucket] = Found;
  }

  if(Found) {
    Found->StorageIndexA = StorageIndexA;
    Found->StorageIndexB = StorageIndexB;
    Found->CanCollide = CanCollide;
  }
}

sim_entity_collision_volume_group *MakeSimpleGroundedCollision(game_state *GameState, real32 DimX, real32 DimY, real32 DimZ) {
  // TODO: NOT WORLD ARENA! Change to using the fundamental types arena, etc
  sim_entity_collision_volume_group *Group = PushStruct(&GameState->WorldArena, sim_entity_collision_volume_group);
  Group->VolumeCount = 1;
  Group->Volumes = PushArray(&GameState->WorldArena, Group->VolumeCount, sim_entity_collision_volume);
  Group->TotalVolume.OffsetP = V3(0, 0, 0.5f * DimZ);
  Group->TotalVolume.Dim = V3(DimX, DimY, DimZ);
  Group->Volumes[0] = Group->TotalVolume;

  return Group;
}

sim_entity_collision_volume_group *MakeNullCollision(game_state *GameState) {
  // TODO: NOT WORLD ARENA! Change to using the fundamental types arena, etc
  sim_entity_collision_volume_group *Group = PushStruct(&GameState->WorldArena, sim_entity_collision_volume_group);
  Group->VolumeCount = 0;
  Group->Volumes = 0;
  Group->TotalVolume.OffsetP = V3(0, 0, 0);
  // TODO: Should this be negative?
  Group->TotalVolume.Dim = V3(0, 0, 0);

  return Group;
}

internal void FillGroundChunk(transient_state *TranState, game_state *GameState, ground_buffer *GroundBuffer, world_position *ChunkP) {
  temporary_memory GroundMemory = BeginTemporaryMemory(&TranState->TranArena);
  // TODO: Decide what our pushbuffer size is!
  render_group *RenderGroup = AllocateRenderGroup(&TranState->TranArena, Megabytes(4), 1.0f);

  Clear(RenderGroup, V4(1.0f, 1.0f, 0.0f, 1.0f));

  loaded_bitmap *Buffer = &GroundBuffer->Bitmap;

  GroundBuffer->P = *ChunkP;

  real32 Width = (real32)Buffer->Width;
  real32 Height = (real32)Buffer->Height;

  for (int32 ChunkOffsetY = -1; ChunkOffsetY <= 1; ++ChunkOffsetY) {
    for (int32 ChunkOffsetX = -1; ChunkOffsetX <= 1; ++ChunkOffsetX) {
      int32 ChunkX = ChunkP->ChunkX + ChunkOffsetX;
      int32 ChunkY = ChunkP->ChunkY + ChunkOffsetY;
      int32 ChunkZ = ChunkP->ChunkZ;

      // TODO: Make random number generation more systemic
      // TODO: Look into wang hashing or some other spatial seed generation "thing"!
      random_series Series = RandomSeed(139 * ChunkX + 593 * ChunkY + 329 * ChunkZ);

      v2 Center = V2(ChunkOffsetX * Width, ChunkOffsetY * Height);

      for (uint32 GrassIndex = 0; GrassIndex < 100; ++GrassIndex) {
        loaded_bitmap *Stamp;

        if (RandomChoice(&Series, 2)) {
          Stamp = GameState->Grass + RandomChoice(&Series, ArrayCount(GameState->Grass));
        } else {
          Stamp = GameState->Stone + RandomChoice(&Series, ArrayCount(GameState->Stone));
        }

        v2 BitmapCenter = 0.5f * V2i(Stamp->Width, Stamp->Height);
        v2 Offset = { Width * RandomUnilateral(&Series), Height * RandomUnilateral(&Series) };
        v2 P = Center + Offset - BitmapCenter;

        PushBitmap(RenderGroup, Stamp, V3(P, 0.0f));
      }
    }
  }

  for (int32 ChunkOffsetY = -1; ChunkOffsetY <= 1; ++ChunkOffsetY) {
    for (int32 ChunkOffsetX = -1; ChunkOffsetX <= 1; ++ChunkOffsetX) {
      int32 ChunkX = ChunkP->ChunkX + ChunkOffsetX;
      int32 ChunkY = ChunkP->ChunkY + ChunkOffsetY;
      int32 ChunkZ = ChunkP->ChunkZ;

      // TODO: Make random number generation more systemic
      // TODO: Look into wang hashing or some other spatial seed generation "thing"!
      random_series Series = RandomSeed(139 * ChunkX + 593 * ChunkY + 329 * ChunkZ);

      v2 Center = V2(ChunkOffsetX * Width, ChunkOffsetY * Height);

      for (uint32 GrassIndex = 0; GrassIndex < 30; ++GrassIndex) {
        loaded_bitmap *Stamp = GameState->Tuft + RandomChoice(&Series, ArrayCount(GameState->Tuft));

        v2 BitmapCenter = 0.5f * V2i(Stamp->Width, Stamp->Height);
        v2 Offset = { Width * RandomUnilateral(&Series), Height * RandomUnilateral(&Series) };
        v2 P = Center + Offset - BitmapCenter;

        PushBitmap(RenderGroup, Stamp, V3(P, 0.0f));
      }
    }
  }

  RenderGroupToOutput(RenderGroup, Buffer);
  EndTemporaryMemory(GroundMemory);
}

internal void ClearBitmap(loaded_bitmap *Bitmap) {
  if (Bitmap->Memory) {
    int32 TotalBitmapSize = Bitmap->Width * Bitmap->Height * BITMAP_BITES_PER_PIXEL;
    ZeroSize(TotalBitmapSize, Bitmap->Memory);
  }
}

internal loaded_bitmap MakeEmptyBitmap(memory_arena *Arena, int32 Width, int32 Height, bool32 ClearToZero = true) {
  loaded_bitmap Result = {};
  Result.Width = Width;
  Result.Height = Height;
  Result.Pitch = Result.Width * BITMAP_BITES_PER_PIXEL;

  int32 TotalBitmapSize = Width * Height * BITMAP_BITES_PER_PIXEL;

  Result.Memory = PushSize(Arena, TotalBitmapSize);

  if (ClearToZero) {
    ClearBitmap(&Result);
  }

  return Result;
}

internal void MakeSphereNormalMap(loaded_bitmap *Bitmap, real32 Roughness, real32 Cx = 1.0f, real32 Cy = 1.0f) {
  real32 InvWidth = 1.0f / (real32)(Bitmap->Width - 1);
  real32 InvHeight = 1.0f / (real32)(Bitmap->Height - 1);

  uint8 *Row = (uint8 *)Bitmap->Memory;
  for(int32 Y = 0; Y < Bitmap->Height; ++Y) {
    uint32 *Pixel = (uint32 *)Row;

    for(int32 X = 0; X < Bitmap->Width; ++X) {
      v2 BitmapUV = { InvWidth * (real32)X, InvHeight * (real32)Y };

      real32 Nx = Cx * (2.0f * BitmapUV.x - 1.0f);
      real32 Ny = Cy * (2.0f * BitmapUV.y - 1.0f);

      real32 RootTerm = 1.0f - Square(Nx) - Square(Ny);
      v3 Normal = { 0, 0.707106781188f, 0.707106781188f};
      real32 Nz = 0.0f;

      if (RootTerm >= 0.0f) {
        Nz = SquareRoot(RootTerm);
        Normal = V3(Nx, Ny, Nz);
      }

      v4 Color = {
        255.0f * (0.5f * (Normal.x + 1.0f )),
        255.0f * (0.5f * (Normal.y + 1.0f )),
        255.0f * (0.5f * (Normal.z + 1.0f )),
        255.0f * Roughness
      };

      *Pixel++ = (
        ((uint32)(Color.a + 0.5f) << 24) |
        ((uint32)(Color.r + 0.5f) << 16) |
        ((uint32)(Color.g + 0.5f) << 8) |
        ((uint32)(Color.b + 0.5f) << 0)
      );
    }

    Row += Bitmap->Pitch;
  }
}

internal void MakeSphereDiffuseMap(loaded_bitmap *Bitmap, real32 Cx = 1.0f, real32 Cy = 1.0f) {
  real32 InvWidth = 1.0f / (real32)(Bitmap->Width - 1);
  real32 InvHeight = 1.0f / (real32)(Bitmap->Height - 1);

  uint8 *Row = (uint8 *)Bitmap->Memory;
  for(int32 Y = 0; Y < Bitmap->Height; ++Y) {
    uint32 *Pixel = (uint32 *)Row;

    for(int32 X = 0; X < Bitmap->Width; ++X) {
      v2 BitmapUV = { InvWidth * (real32)X, InvHeight * (real32)Y };

      real32 Nx = Cx * (2.0f * BitmapUV.x - 1.0f);
      real32 Ny = Cy * (2.0f * BitmapUV.y - 1.0f);

      real32 RootTerm = 1.0f - Square(Nx) - Square(Ny);
      real32 Alpha = 0.0f;
      if (RootTerm >= 0.0f) {
        Alpha = 1.0f;
      }

      v3 BaseColor = {  0.0f, 0.0f, 0.0f };
      Alpha *= 255.0f;
      v4 Color = {
        Alpha * BaseColor.x,
        Alpha * BaseColor.y,
        Alpha * BaseColor.z,
        Alpha
      };

      *Pixel++ = (
        ((uint32)(Color.a + 0.5f) << 24) |
        ((uint32)(Color.r + 0.5f) << 16) |
        ((uint32)(Color.g + 0.5f) << 8) |
        ((uint32)(Color.b + 0.5f) << 0)
      );
    }

    Row += Bitmap->Pitch;
  }
}

internal void MakePyramidNormalMap(loaded_bitmap *Bitmap, real32 Roughness) {
  real32 InvWidth = 1.0f / (real32)(Bitmap->Width - 1);
  real32 InvHeight = 1.0f / (real32)(Bitmap->Height - 1);

  uint8 *Row = (uint8 *)Bitmap->Memory;
  for(int32 Y = 0; Y < Bitmap->Height; ++Y) {
    uint32 *Pixel = (uint32 *)Row;

    for(int32 X = 0; X < Bitmap->Width; ++X) {
      v2 BitmapUV = { InvWidth * (real32)X, InvHeight * (real32)Y };

      int32 InvX = (Bitmap->Width - 1) - X;
      real32 Seven = 0.707106781188f;
      v3 Normal = {0, 0, Seven };

      if (X < Y) {
        if(InvX < Y) {
          Normal.x = -Seven;
        } else {
          Normal.y = Seven;
        }
      } else {
        if (InvX < Y) {
          Normal.y = -Seven;
        } else {
          Normal.x = Seven;
        }
      }

      v4 Color = {
        255.0f * (0.5f * (Normal.x + 1.0f )),
        255.0f * (0.5f * (Normal.y + 1.0f )),
        255.0f * (0.5f * (Normal.z + 1.0f )),
        255.0f * Roughness
      };

      *Pixel++ = (
        ((uint32)(Color.a + 0.5f) << 24) |
        ((uint32)(Color.r + 0.5f) << 16) |
        ((uint32)(Color.g + 0.5f) << 8) |
        ((uint32)(Color.b + 0.5f) << 0)
      );
    }

    Row += Bitmap->Pitch;
  }
}


#if 0
internal void RequestGroundBuffers(world_position CenterP, rectangle3 Bounds) {
  Bounds = Offset(Bounds, CenterP.Offset);
  CenterP.Offset = V3(0, 0, 0);

  for(uint32 ) {

  }
  // TODO: This is just a test fill
  FillGroundChunk(TranState, GameState, TranState->GroundBuffers, &GameState->CameraP);
}
#endif

internal void SetTopDownAlign(hero_bitmaps *Bitmap, v2 Align) {
  Align = TopDownAlign(&Bitmap->Head, Align);
  Bitmap->Head.Align = Align;
  Bitmap->Cape.Align = Align;
  Bitmap->Torso.Align = Align;
}

extern "C" GAME_UPDATE_AND_RENDER(GameUpdateAndRender) {
  Assert((&Input->Controllers[0].Terminator - &Input->Controllers[0].Buttons[0]) == (ArrayCount(Input->Controllers[0].Buttons)));

  uint32 GroundBufferWidth = 256;
  uint32 GroundBufferHeight = 256;

  Assert(sizeof(game_state) <= Memory->PermanentStorageSize);

  game_state *GameState = (game_state *)Memory->PermanentStorage;

  if (!Memory->IsInitialized) {
    uint32 TilesPerWidth = 17;
    uint32 TilesPerHeight = 9;

    GameState->TypicalFloorHeight = 3.0f;
    GameState->MetersToPixels = 42.0f;
    GameState->PixelsToMeters = 1.0f / GameState->MetersToPixels;

    v3 WorldChunkDimInMeters = {
      GameState->PixelsToMeters * (real32)GroundBufferWidth,
      GameState->PixelsToMeters * (real32)GroundBufferHeight,
      GameState->TypicalFloorHeight
    };

    InitializeArena(&GameState->WorldArena, Memory->PermanentStorageSize - sizeof(game_state), (uint8 *)Memory->PermanentStorage + sizeof(game_state));

    // NOTE: Reserve entity slot 0 for the null entity
    AddLowEntity(GameState, EntityType_Null, NullPosition());

    GameState->World = PushStruct(&GameState->WorldArena, world);
    world *World = GameState->World;

    InitializeWorld(World, WorldChunkDimInMeters);

    real32 TileSideInMeters = 1.4f;
    real32 TileDepthInMeters = GameState->TypicalFloorHeight;

    GameState->NullCollision = MakeNullCollision(GameState);
    GameState->SwordCollision = MakeSimpleGroundedCollision(GameState, 1.0f, 0.5f, 0.1f);
    GameState->StairCollision = MakeSimpleGroundedCollision(GameState,  TileSideInMeters, 2.0f * TileSideInMeters, 1.1f * TileDepthInMeters);
    GameState->PlayerCollision = MakeSimpleGroundedCollision(GameState, 1.0f, 0.5f, 1.2f);
    GameState->MonsterCollision = MakeSimpleGroundedCollision(GameState, 1.0f, 0.5f, 0.5f);
    GameState->FamiliarCollision = MakeSimpleGroundedCollision(GameState, 1.0f, 0.5f, 0.5f);
    GameState->WallCollision = MakeSimpleGroundedCollision(GameState, TileSideInMeters, TileSideInMeters, TileDepthInMeters);
    GameState->StandardRoomCollision = MakeSimpleGroundedCollision(GameState, TilesPerWidth * TileSideInMeters, TilesPerHeight * TileSideInMeters, 0.9f * TileDepthInMeters);

    GameState->Grass[0] = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test2/grass00.bmp");
    GameState->Grass[1] = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test2/grass01.bmp");
    GameState->Tuft[0] = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test2/tuft00.bmp");
    GameState->Tuft[1] = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test2/tuft01.bmp");
    GameState->Tuft[2] = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test2/tuft02.bmp");
    GameState->Stone[0] = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test2/ground00.bmp");
    GameState->Stone[1] = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test2/ground01.bmp");
    GameState->Stone[2] = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test2/ground02.bmp");
    GameState->Stone[3] = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test2/ground03.bmp");

    GameState->Backdrop = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_background.bmp");
    GameState->Shadow = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_hero_shadow.bmp", 72, 182);
    GameState->Tree = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test2/tree00.bmp", 40, 80);
    GameState->Stairwell = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test2/rock02.bmp");
    GameState->Sword = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test2/rock03.bmp", 29, 10);

    hero_bitmaps *Bitmap;

    Bitmap = GameState->HeroBitmaps;

    Bitmap->Head = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_hero_right_head.bmp");
    Bitmap->Cape = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_hero_right_cape.bmp");
    Bitmap->Torso = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_hero_right_torso.bmp");
    SetTopDownAlign(Bitmap, V2(72, 182));
    ++Bitmap;

    Bitmap->Head  = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_hero_back_head.bmp");
    Bitmap->Cape = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_hero_back_cape.bmp");
    Bitmap->Torso = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_hero_back_torso.bmp");
    SetTopDownAlign(Bitmap, V2(72, 182));
    ++Bitmap;

    Bitmap->Head = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_hero_left_head.bmp");
    Bitmap->Cape = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_hero_left_cape.bmp");
    Bitmap->Torso = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_hero_left_torso.bmp");
    SetTopDownAlign(Bitmap, V2(72, 182));
    ++Bitmap;

    Bitmap->Head = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_hero_front_head.bmp");
    Bitmap->Cape = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_hero_front_cape.bmp");
    Bitmap->Torso = DEBUGLoadBMP(Thread, Memory->DEBUGPlatformReadEntireFile, "test/test_hero_front_torso.bmp");
    SetTopDownAlign(Bitmap, V2(72, 182));
    ++Bitmap;

    random_series Series = RandomSeed(1234);
    uint32 ScreenBaseX = 0;
    uint32 ScreenBaseY = 0;
    uint32 ScreenBaseZ = 0;
    uint32 ScreenX = ScreenBaseX;
    uint32 ScreenY = ScreenBaseY;
    uint32 AbsTileZ = ScreenBaseZ;

    // TODO: Replace all this with real world generation!
    bool32 DoorLeft = false;
    bool32 DoorRight = false;
    bool32 DoorTop = false;
    bool32 DoorBottom = false;
    bool32 DoorUp = false;
    bool32 DoorDown = false;

    for (uint32 ScreenIndex = 0; ScreenIndex < 2000; ++ScreenIndex) {
#if 1
      uint32 DoorDirection = RandomChoice(&Series, (DoorUp || DoorDown) ? 2 : 4);
#else
      uint32 DoorDirection = RandomChoice(&Series, 2);
#endif

      DoorDirection = 3;
      bool32 CreatedZDoor = false;

      if (DoorDirection == 3) {
        CreatedZDoor = true;
        DoorDown = true;
      } else if (DoorDirection == 2) {
        CreatedZDoor = true;
        DoorUp = true;
      } else if (DoorDirection == 1) {
        DoorRight = true;
      } else {
        DoorTop = true;
      }

      AddStandardRoom(GameState, ScreenX * TilesPerWidth + TilesPerWidth / 2, ScreenY * TilesPerHeight + TilesPerHeight / 2, AbsTileZ);

      for (uint32 TileY = 0; TileY < TilesPerHeight; ++TileY) {
        for (uint32 TileX = 0; TileX < TilesPerWidth; ++TileX) {
          uint32 AbsTileX = ScreenX * TilesPerWidth + TileX;
          uint32 AbsTileY = ScreenY * TilesPerHeight + TileY;

          bool32 ShouldBeDoor = false;
          if ((TileX == 0) && (!DoorLeft || (TileY != (TilesPerHeight / 2)))) {
            ShouldBeDoor = true;
          }
          if ((TileX == (TilesPerWidth - 1)) && (!DoorRight || (TileY != (TilesPerHeight / 2)))) {
            ShouldBeDoor = true;
          }
          if ((TileY == 0) && (!DoorBottom || (TileX != (TilesPerWidth / 2)))) {
            ShouldBeDoor = true;
          }
          if ((TileY == (TilesPerHeight - 1)) && (!DoorTop || (TileX != (TilesPerWidth / 2)))) {
            ShouldBeDoor = true;
          }

          if (ShouldBeDoor) {
            if ((TileY % 2) || (TileX % 2)) {
              AddWall(GameState, AbsTileX, AbsTileY, AbsTileZ);
            }
          } else if (CreatedZDoor) {
            if (((AbsTileZ % 2) && (TileX == 10) && (TileY == 5)) || (!(AbsTileZ % 2) && (TileX == 4) && (TileY == 5))) {
              AddStair(GameState, AbsTileX, AbsTileY, DoorDown ? AbsTileZ - 1 : AbsTileZ);
            }
          }
        }
      }

      DoorLeft = DoorRight;
      DoorBottom = DoorTop;

      if (CreatedZDoor) {
        DoorDown = !DoorDown;
        DoorUp = !DoorUp;
      } else {
        DoorUp = false;
        DoorDown = false;
      }

      DoorRight = false;
      DoorTop = false;

      if (DoorDirection == 3) {
        AbsTileZ -=1;
      } else if (DoorDirection == 2) {
        AbsTileZ += 1;
      } else if (DoorDirection == 1) {
        ScreenX += 1;
      } else {
        ScreenY += 1;
      }
    }

#if 0 // NOTE: performance test code, we can remove it
    while (GameState->LowEntityCount < ArrayCount(GameState->LowEntities) - 16) {
      uint32 Coordinate = 1024 + GameState->LowEntityCount;
      AddWall(GameState, Coordinate, Coordinate, Coordinate);
    }
#endif

    world_position NewCameraP = {};
    uint32 CameraTileX = ScreenBaseX * TilesPerWidth + 17 / 2;
    uint32 CameraTileY = ScreenBaseY * TilesPerHeight + 9 / 2;
    uint32 CameraTileZ = ScreenBaseZ;
    NewCameraP = ChunkPositionFromTilePosition(GameState->World, CameraTileX, CameraTileY, CameraTileZ);
    GameState->CameraP = NewCameraP;

    AddMonster(GameState, CameraTileX - 3, CameraTileY + 2, CameraTileZ);
    for (int FamiliarIndex = 0; FamiliarIndex < 1; ++FamiliarIndex) {
      int32 FamiliarOffsetX = RandomBetween(&Series, -7, 7);
      int32 FamiliarOffsetY = RandomBetween(&Series, -3, -1);

      if (FamiliarOffsetX != 0 || FamiliarOffsetY != 0) {
        AddFamiliar(GameState, CameraTileX + FamiliarOffsetX, CameraTileY + FamiliarOffsetY, CameraTileZ);
      }
    }

    Memory->IsInitialized = true;
  }


  // NOTE: Transient initialization
  Assert(sizeof(transient_state) <= Memory->TransientStorageSize);
  transient_state *TranState = (transient_state *)Memory->TransientStorage;
  if (!TranState->IsInitialized) {
    InitializeArena(&TranState->TranArena, Memory->TransientStorageSize - sizeof(transient_state), (uint8 *)Memory->TransientStorage + sizeof(transient_state));
    // TODO: Pick a real number here!
    TranState->GroundBufferCount = 64;
    TranState->GroundBuffers = PushArray(&TranState->TranArena, TranState->GroundBufferCount, ground_buffer);

    for(uint32 GroundBufferIndex = 0; GroundBufferIndex < TranState->GroundBufferCount; ++GroundBufferIndex) {
      ground_buffer *GroundBuffer = TranState->GroundBuffers + GroundBufferIndex;
      GroundBuffer->Bitmap = MakeEmptyBitmap(&TranState->TranArena, GroundBufferWidth, GroundBufferHeight, false);
      GroundBuffer->P = NullPosition();
    }

    GameState->TestDiffuse = MakeEmptyBitmap(&TranState->TranArena, 256, 256, false);
    DrawRectangle(&GameState->TestDiffuse, V2(0, 0), V2i( GameState->TestDiffuse.Width, GameState->TestDiffuse.Height), V4(0.5f, 0.5f, 0.5f, 1.0f));
    GameState->TestNormal = MakeEmptyBitmap(&TranState->TranArena, GameState->TestDiffuse.Width, GameState->TestDiffuse.Height, false);

    MakeSphereNormalMap(&GameState->TestNormal, 0.0f);
    MakeSphereDiffuseMap(&GameState->TestDiffuse);
    // MakePyramidNormalMap(&GameState->TestNormal, 0.0f);


    TranState->EnvMapWidth = 512;
    TranState->EnvMapHeight = 256;

    for (uint32 MapIndex = 0; MapIndex < ArrayCount(TranState->EnvMaps); ++MapIndex) {
      environment_map *Map = TranState->EnvMaps + MapIndex;
      uint32 Width = TranState->EnvMapWidth;
      uint32 Height = TranState->EnvMapHeight;
      for (uint32 LODIndex = 0; LODIndex < ArrayCount(Map->LOD); ++LODIndex) {
        Map->LOD[LODIndex] = MakeEmptyBitmap(&TranState->TranArena, Width, Height, false);
        Width >>= 1;
        Height >>= 1;
      }
    }

    TranState->IsInitialized = true;
  }

#if 0
  if (Input->ExecutableReload) {
    for(uint32 GroundBufferIndex = 0; GroundBufferIndex < TranState->GroundBufferCount; ++GroundBufferIndex) {
      ground_buffer *GroundBuffer = TranState->GroundBuffers + GroundBufferIndex;
      GroundBuffer->P = NullPosition();
    }
  }
#endif

  world *World = GameState->World;
  real32 MetersToPixels = GameState->MetersToPixels;
  real32 PixelsToMeters = 1.0f / MetersToPixels;

  //
  // NOTE: ???
  //
  for (int ControllerIndex = 0; ControllerIndex < ArrayCount(Input->Controllers); ++ControllerIndex) {
    game_controller_input *Controller = GetController(Input, ControllerIndex);
    controlled_hero *ConHero = GameState->ControlledHeroes + ControllerIndex;

    if (ConHero->EntityIndex == 0) {
      if (Controller->Start.EndedDown) {
        *ConHero = {};
        ConHero->EntityIndex = AddPlayer(GameState).LowIndex;
      }
    } else {
      ConHero->dZ = 0.0f;
      ConHero->ddP = {}; // acceleration
      ConHero->dSword = {};

      if (Controller->IsAnalog) {
        // NOTE: Use analog movement tuning

        ConHero->ddP = V2(Controller->StickAverageX, Controller->StickAverageY);
      } else {
        // NOTE: Use digital movement tuning
        if (Controller->MoveUp.EndedDown) {
          ConHero->ddP.y = 1.0f;
        }
        if (Controller->MoveDown.EndedDown) {
          ConHero->ddP.y = -1.0f;
        }
        if (Controller->MoveLeft.EndedDown) {
          ConHero->ddP.x = -1.0f;
        }
        if (Controller->MoveRight.EndedDown) {
          ConHero->ddP.x = 1.0f;
        }
      }

      if (Controller->Start.EndedDown) {
        ConHero->dZ = 3.0f;
      }

      ConHero->dSword = {};

      if (Controller->ActionUp.EndedDown) {
        ConHero->dSword = V2(0.0f, 1.0f);
      }
      if (Controller->ActionDown.EndedDown) {
        ConHero->dSword = V2(0.0f, -1.0f);
      }
      if (Controller->ActionLeft.EndedDown) {
        ConHero->dSword = V2(-1.0f, 0.0f);
      }
      if (Controller->ActionRight.EndedDown) {
        ConHero->dSword = V2(1.0f, 0.0f);
      }
    }
  }

  //
  // NOTE: Render
  //
  temporary_memory RenderMemory = BeginTemporaryMemory(&TranState->TranArena);
  // TODO: Decide what our pushbuffer size is!
  render_group *RenderGroup = AllocateRenderGroup(&TranState->TranArena, Megabytes(4), GameState->MetersToPixels);

  loaded_bitmap DrawBuffer_ = {};
  loaded_bitmap *DrawBuffer = &DrawBuffer_;
  DrawBuffer->Width = Buffer->Width;
  DrawBuffer->Height = Buffer->Height;
  DrawBuffer->Pitch = Buffer->Pitch;
  DrawBuffer->Memory = Buffer->Memory;

  Clear(RenderGroup, V4(0.25f, 0.25f, 0.25f, 0.0f));

  v2 ScreenCenter = {
    0.5f * (real32)DrawBuffer->Width,
    0.5f * (real32)DrawBuffer->Height
  };

  real32 ScreenWidthInMeters = DrawBuffer->Width * PixelsToMeters;
  real32 ScreenHeightInMeters = DrawBuffer->Height * PixelsToMeters;
  rectangle3 CameraBoundsInMeters = RectCenterDim(V3(0, 0, 0), V3(ScreenWidthInMeters, ScreenHeightInMeters, 0.0f));

  CameraBoundsInMeters.Min.z = -3.0f * GameState->TypicalFloorHeight;
  CameraBoundsInMeters.Max.z = 1.0f * GameState->TypicalFloorHeight;

#if 0
  for(uint32 GroundBufferIndex = 0; GroundBufferIndex < TranState->GroundBufferCount; ++GroundBufferIndex) {
    ground_buffer *GroundBuffer = TranState->GroundBuffers + GroundBufferIndex;

    if (IsValid(GroundBuffer->P)) {
      loaded_bitmap *Bitmap = &GroundBuffer->Bitmap;
      v3 Delta = Subtract(GameState->World, &GroundBuffer->P, &GameState->CameraP);
      Bitmap->Align = 0.5f * V2i(Bitmap->Width, Bitmap->Height);

      render_basis *Basis = PushStruct(&TranState->TranArena, render_basis);
      RenderGroup->DefaultBasis = Basis;
      Basis->P = Delta + V3(0, 0, GameState->ZOffset);
      PushBitmap(RenderGroup, Bitmap, V3(0, 0, 0));
    }
  }

  //
  // NOTE: Ground management
  //
  {
    world_position MinChunkP = MapIntoChunkSpace(World, GameState->CameraP, GetMinCorner(CameraBoundsInMeters));
    world_position MaxChunkP = MapIntoChunkSpace(World, GameState->CameraP, GetMaxCorner(CameraBoundsInMeters));

    for(int32 ChunkZ = MinChunkP.ChunkZ; ChunkZ <= MaxChunkP.ChunkZ; ++ChunkZ) {
      for(int32 ChunkY = MinChunkP.ChunkY; ChunkY <= MaxChunkP.ChunkY; ++ChunkY) {
        for(int32 ChunkX = MinChunkP.ChunkX; ChunkX <= MaxChunkP.ChunkX; ++ChunkX) {
          //world_chunk *Chunk = GetWorldChunk(World, ChunkX, ChunkY, ChunkZ);

          //if (Chunk)
          {
            world_position ChunkCenterP = CenteredChunkPoint(ChunkX, ChunkY, ChunkZ);
            v3 RelP = Subtract(World, &ChunkCenterP, &GameState->CameraP);


            // TODO: This is super inefficient, fix it!
            real32 FurthestBufferLengthSq = 0.0f;
            ground_buffer *FurthestBuffer = 0;
            for(uint32 GroundBufferIndex = 0; GroundBufferIndex < TranState->GroundBufferCount; ++GroundBufferIndex) {
              ground_buffer *GroundBuffer = TranState->GroundBuffers + GroundBufferIndex;

              if (AreInSameChunk(World, &GroundBuffer->P, &ChunkCenterP)) {
                FurthestBuffer = 0;
                break;
              } else if (IsValid(GroundBuffer->P)) {
                v3 RelP = Subtract(World, &GroundBuffer->P, &GameState->CameraP);
                real32 BufferLengthSq = LengthSq(RelP.xy);

                if (FurthestBufferLengthSq < BufferLengthSq) {
                  FurthestBufferLengthSq = BufferLengthSq;
                  FurthestBuffer = GroundBuffer;
                }
              } else {
                FurthestBufferLengthSq = Real32Maximum;
                FurthestBuffer = GroundBuffer;
              }
            }

            if (FurthestBuffer) {
              FillGroundChunk(TranState, GameState, FurthestBuffer, &ChunkCenterP);
            }
#if 0
            PushRectOutline(RenderGroup, RelP.xy, 0.0f, World->ChunkDimInMeters.xy, V4(1.0f, 1.0f, 0.0f, 1.0f));
#endif
          }
        }
      }
    }
  }
#endif
  // TODO: How big do we actually want to expand here?
  // TODO: Do we want to simulate upper floors, etc?
  v3 SimBoundsExpansion = { 15.0f, 15.0f, 0.0f };
  rectangle3 SimBounds = AddRadiusTo(CameraBoundsInMeters, SimBoundsExpansion);

  temporary_memory SimMemory = BeginTemporaryMemory(&TranState->TranArena);
  world_position SimCenterP = GameState->CameraP;
  sim_region *SimRegion = BeginSim(&TranState->TranArena, GameState, GameState->World, SimCenterP, SimBounds, Input->dtForFrame);

  v3 CameraP = Subtract(World, &GameState->CameraP, &SimCenterP);

  // TODO: Move this out into handmade_entity.cpp!
  for(uint32 EntityIndex = 0; EntityIndex < SimRegion->EntityCount; ++EntityIndex) {
    sim_entity *Entity = SimRegion->Entities + EntityIndex;

    if (Entity->Updatable) {
      real32 dt = Input->dtForFrame;

      // TODO: This is incorrect, should be computed after update!!
      real32 ShadowAlpha = 1.0f - 0.5f * Entity->P.z;
      if (ShadowAlpha < 0) {
        ShadowAlpha = 0.0f;
      }

      move_spec MoveSpec = DefaultMoveSpec();
      v3 ddP = {};

      render_basis *Basis = PushStruct(&TranState->TranArena, render_basis);
      RenderGroup->DefaultBasis = Basis;

      // TODO: Probably indicates we want to separate update and render
      // for entities sometime soon?
      v3 CameraRelativeGroundP = GetEntityGroundPoint(Entity) - CameraP;
      real32 FadeTopEndZ = 0.75f * GameState->TypicalFloorHeight;
      real32 FadeTopStartZ = 0.5f * GameState->TypicalFloorHeight;
      real32 FadeBottomStartZ = -2.0f * GameState->TypicalFloorHeight;
      real32 FadeBottomEndZ = -2.25f * GameState->TypicalFloorHeight;
      RenderGroup->GlobalAlpha = 1.0f;

      if (CameraRelativeGroundP.z > FadeTopStartZ) {
        RenderGroup->GlobalAlpha = Clamp01MapToRange(FadeTopEndZ, CameraRelativeGroundP.z, FadeTopStartZ);
      } else if (CameraRelativeGroundP.z < FadeBottomStartZ) {
        RenderGroup->GlobalAlpha = Clamp01MapToRange(FadeBottomEndZ, CameraRelativeGroundP.z, FadeBottomStartZ);
      }

      hero_bitmaps *HeroBitmaps = &GameState->HeroBitmaps[Entity->FacingDirection];

      switch (Entity->Type) {
        case EntityType_Hero: {
          // TODO: Now that we have some real usage examples, lets solidify the positioning system
          for(uint32 ControlIndex = 0; ControlIndex < ArrayCount(GameState->ControlledHeroes); ++ControlIndex) {
            controlled_hero *ConHero = GameState->ControlledHeroes + ControlIndex;

            if (Entity->StorageIndex == ConHero->EntityIndex) {
              if (ConHero->dZ != 0.0f) {
                Entity->dP.z = ConHero->dZ;
              }

              MoveSpec.UnitMaxAccelVector = true;
              MoveSpec.Speed = 50.0f;
              MoveSpec.Drag = 8.0f;
              ddP = V3(ConHero->ddP, 0);

              if ((ConHero->dSword.x != 0.0f) || (ConHero->dSword.y != 0.0f)) {
                sim_entity *Sword = Entity->Sword.Ptr;
                if (Sword && IsSet(Sword, EntityFlag_Nonspatial)) {
                  Sword->DistanceLimit = 5.0f;
                  MakeEntitySpatial(Sword, Entity->P, Entity->dP + 5.0f * V3(ConHero->dSword, 0));
                  AddCollisionRule(GameState, Sword->StorageIndex, Entity->StorageIndex, false);
                }
              }
            }
          }

          // TODO: Z!!!
          PushBitmap(RenderGroup, &GameState->Shadow, V3(0, 0, 0), V4(1, 1, 1, ShadowAlpha));
          PushBitmap(RenderGroup, &HeroBitmaps->Torso, V3(0, 0, 0));
          PushBitmap(RenderGroup, &HeroBitmaps->Cape, V3(0, 0, 0));
          PushBitmap(RenderGroup, &HeroBitmaps->Head, V3(0, 0, 0));

          DrawHitpoints(Entity, RenderGroup);
        } break;

        case EntityType_Wall: {
          PushBitmap(RenderGroup, &GameState->Tree, V3(0, 0, 0));
        } break;

        case EntityType_Stairwell: {
          PushRect(RenderGroup, V3(0, 0, 0), Entity->WalkableDim, V4(1, 0.5f, 0, 1));
          PushRect(RenderGroup, V3(0, 0, Entity->WalkableHeight), Entity->WalkableDim, V4(1, 1, 0, 1));
        } break;

        case EntityType_Sword: {
          MoveSpec.UnitMaxAccelVector = false;
          MoveSpec.Speed = 0.0f;
          MoveSpec.Drag = 0.0f;

          if (Entity->DistanceLimit == 0.0f) {
            ClearCollisionRulesFor(GameState, Entity->StorageIndex);
            MakeEntityNonSpatial(Entity);
          }
          PushBitmap(RenderGroup, &GameState->Shadow, V3(0, 0, 0), V4(1, 1, 1, ShadowAlpha));
          PushBitmap(RenderGroup, &GameState->Sword, V3(0, 0, 0));
        } break;

        case EntityType_Familiar: {
          sim_entity *ClosestHero = 0;
          real32 ClosestHeroDSq = Square(10.0f); //NOTE: Ten meter square maximum search!

#if 0
          // TODO: Make spatial queries easy for things!
          sim_entity *TestEntity = SimRegion->Entities;
          for(uint32 TestEntityIndex = 0; TestEntityIndex < SimRegion->EntityCount; ++TestEntityIndex, ++TestEntity) {
            if (TestEntity->Type == EntityType_Hero) {
              real32 TestDSq = LengthSq(TestEntity->P - Entity->P);
              if (ClosestHeroDSq > TestDSq) {
                ClosestHero = TestEntity;
                ClosestHeroDSq = TestDSq;
              }
            }
          }
#endif
          if (ClosestHero && (ClosestHeroDSq > Square(3.0f))) {
            real32 Acceleration = 0.5f;
            real32 OneOverLenght = Acceleration / SquareRoot(ClosestHeroDSq);
            ddP = OneOverLenght * (ClosestHero->P - Entity->P);
          }

          MoveSpec.UnitMaxAccelVector = true;
          MoveSpec.Speed = 50.0f;
          MoveSpec.Drag = 8.0f;

          Entity->tBob += dt;
          if (Entity->tBob > (2.0f * Pi32)) {
            Entity->tBob -= (2.0f * Pi32);
          }
          real32 BobSin = Sin(2.0f * Entity->tBob);

          PushBitmap(RenderGroup, &GameState->Shadow, V3(0, 0, 0), V4(1, 1, 1, (0.5f * ShadowAlpha) + 0.2f * BobSin));
          PushBitmap(RenderGroup, &HeroBitmaps->Head, V3(0, 0, 0.25f * BobSin));
        } break;

        case EntityType_Monster: {
          PushBitmap(RenderGroup, &GameState->Shadow, V3(0, 0, 0), V4(1, 1, 1, ShadowAlpha));
          PushBitmap(RenderGroup, &HeroBitmaps->Torso, V3(0, 0, 0));
          DrawHitpoints(Entity, RenderGroup);
        } break;

        case EntityType_Space: {
          for (uint32 VolumeIndex = 0; VolumeIndex < Entity->Collision->VolumeCount; ++VolumeIndex) {
            sim_entity_collision_volume *Volume = Entity->Collision->Volumes + VolumeIndex;
            PushRectOutline(RenderGroup, Volume->OffsetP - V3(0, 0, 0.5f * Volume->Dim.z), Volume->Dim.xy, V4(0, 0.5f, 1.0f, 1));
          }

        } break;

        default: {
          InvalidCodePath;
        } break;
       }

      if (!IsSet(Entity, EntityFlag_Nonspatial) && IsSet(Entity, EntityFlag_Moveable)) {
        MoveEntity(GameState, SimRegion, Entity, Input->dtForFrame, &MoveSpec, ddP);
      }

      Basis->P = GetEntityGroundPoint(Entity);
    }
  }

  RenderGroup->GlobalAlpha = 1.0f;
#if 0
  GameState->Time += Input->dtForFrame;

  v3 MapColor[] = {
    {1, 0, 0},
    {0, 1, 0},
    {0, 0, 1}
  };

  for(uint32 MapIndex = 0; MapIndex < ArrayCount(TranState->EnvMaps); ++MapIndex) {
    environment_map *Map = TranState->EnvMaps + MapIndex;
    loaded_bitmap *LOD = Map->LOD + 0;
    bool32 RowCheckerOn = false;
    int32 CheckerWidth = 16;
    int32 CheckerHeight = 16;

    for (int32 Y = 0; Y < LOD->Height; Y += CheckerHeight) {
      bool32 CheckerOn = RowCheckerOn;
      for (int32 X = 0; X < LOD->Width; X += CheckerWidth) {
        v4 Color = CheckerOn ? V4(MapColor[MapIndex], 1.0f) : V4(0, 0, 0, 1);
        v2 MinP = V2i(X, Y);
        v2 MaxP = MinP + V2i(CheckerWidth, CheckerHeight);
        DrawRectangle(LOD, MinP, MaxP, Color);
        CheckerOn = !CheckerOn;
      }
      RowCheckerOn = !RowCheckerOn;
    }
  }
  TranState->EnvMaps[0].Pz = -1.5f;
  TranState->EnvMaps[1].Pz = 0.0f;
  TranState->EnvMaps[2].Pz = 1.5f;

  DrawBitmap(TranState->EnvMaps[0].LOD + 0, &TranState->GroundBuffers[TranState->GroundBufferCount - 1].Bitmap, 125.0f, 50.0f, 1.0f);

//  Angle = 0.0f;

  // TODO: Lets add a Perp operator!!!
  v2 Origin = ScreenCenter;
  real32 Angle = 0.1f * GameState->Time;
  #if 1
  v2 Disp = {
    100.0f * Cos(10.0f * Angle),
    100.0f * Sin(3.0f * Angle),
  };
  #else
  v2 Disp = {};
  #endif

  #if 1
  v2 XAxis = 100.0f * V2(Cos(10.0f * Angle), Sin(10.0f * Angle));
  v2 YAxis = Perp(XAxis);
  #else
    v2 XAxis = { 60.0f, 0};
    v2 YAxis = Perp(XAxis);
  #endif
  uint32 PIndex = 0;
  real32 CAngle = 5.0f * Angle;
  #if 0
  v4 Color = V4(
    0.5f + 0.5f * Sin(CAngle),
    0.5f + 0.5f * Sin(2.9f * CAngle),
    0.5f + 0.5f * Cos(9.9f * CAngle),
    0.5f + 0.5F * Sin(10.0f * CAngle)
  );
#else
  v4 Color = V4(1.0f, 1.0f, 1.0f, 1.0f);
#endif
  render_entry_coordinate_system *C = PushCoordninateSystem(
    RenderGroup,
    Disp + Origin - 0.5f * XAxis - 0.5f * YAxis,
    XAxis,
    YAxis,
    Color,
    &GameState->TestDiffuse,
    &GameState->TestNormal,
    TranState->EnvMaps + 2,
    TranState->EnvMaps + 1,
    TranState->EnvMaps + 0
  );

  v2 MapP = { 0.0f, 0.0f};
  for(uint32 MapIndex = 0; MapIndex < ArrayCount(TranState->EnvMaps); ++MapIndex) {
    environment_map *Map = TranState->EnvMaps + MapIndex;
    loaded_bitmap *LOD = Map->LOD + 0;
    XAxis = 0.5f * V2((real32)LOD->Width, 0.0f);
    YAxis = 0.5f * V2(0.0f, (real32)LOD->Height);

    PushCoordninateSystem(RenderGroup, MapP, XAxis, YAxis, V4(1.0f, 1.0f, 1.0f, 1.0f), LOD, 0, 0, 0, 0);
    MapP += YAxis + V2(0.0f, 6.0f);
  }
#endif

  RenderGroupToOutput(RenderGroup, DrawBuffer);

  EndSim(SimRegion, GameState);
  EndTemporaryMemory(SimMemory);
  EndTemporaryMemory(RenderMemory);

  CheckArena(&GameState->WorldArena);
  CheckArena(&TranState->TranArena);
}

extern "C" GAME_GET_SOUND_SAMPLES(GameGetSoundSamples) {
  game_state *GameState = (game_state *)Memory->PermanentStorage;
  GameOutputSound(GameState, SoundBuffer, 400);
}
