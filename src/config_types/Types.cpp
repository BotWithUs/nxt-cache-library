#include "config_types/Types.h"

void VarbitType::decode(RSBuffer &buffer)
{
    while (true)
    {
        unsigned char opcode = buffer.readUnsignedByte();
        if (opcode == 0)
        {
            break;
        }
        if (opcode == 1)
        {
            domainType = buffer.readUnsignedByte();
            varId = buffer.readUnsignedShort();
        }
        else if (opcode == 2)
        {
            lsb = buffer.readUnsignedByte();
            msb = buffer.readUnsignedByte();
        }
    }
}

void NpcType::decode(RSBuffer &buffer)
{
    while (buffer.remaining() > 0)
    {
        unsigned char opcode = buffer.readUnsignedByte();
        if (opcode == 0)
        {
            break;
        }

        if (opcode == 1)
        {
            int count = buffer.readUnsignedByte();
            for (int i = 0; i < count; i++)
            {
                buffer.readSmartInt();
            }
        }
        else if (opcode == 2)
        {
            name = buffer.readString();
        }
        else if (opcode == 3)
        {
            buffer.skip(1);
        }
        else if (opcode >= 30 && opcode <= 34)
        {
            options[opcode - 30] = buffer.readString();
        }
        else if (opcode == 12)
        {
            buffer.skip(1);
        }
        else if (opcode == 40)
        {
            int count = buffer.readUnsignedByte();
            for (int i = 0; i < count; ++i)
            {
                buffer.skip(4);
            }
        }
        else if (opcode == 41)
        {
            int count = buffer.readUnsignedByte();
            for (int i = 0; i < count; ++i)
            {
                buffer.skip(4);
            }
        }
        else if (opcode == 42)
        {
            int count = buffer.readUnsignedByte();
            for (int i = 0; i < count; ++i)
            {
                buffer.skip(1);
            }
        }
        else if (opcode == 44 || opcode == 45 || opcode == 252)
        {
            buffer.skip(2);
        }
        else if (opcode == 60)
        {
            int count = buffer.readUnsignedByte();
            for (int i = 0; i < count; ++i)
            {
                buffer.readSmartInt();
            }
        }
        else if (opcode == 93)
        {
            drawMapDot = false;
        }
        else if (opcode == 95)
        {
            combatLevel = buffer.readUnsignedShort();
        }
        else if (opcode == 97 || opcode == 98)
        {
            buffer.skip(2);
        }
        else if (opcode == 99)
        {
            buffer.skip(0);
        }
        else if (opcode == 100 || opcode == 101)
        {
            buffer.skip(1);
        }
        else if (opcode == 102)
        {
            int vv = buffer.readUnsignedByte();
            int t = 0;
            for (int i = vv; i != 0; i >>= 1)
            {
                t++;
            }
            for (int i = 0; i < t; i++)
            {
                if ((vv & 1 << i) != 0)
                {
                    buffer.readSmartInt();
                    buffer.readSmart();
                }
            }
        }
        else if (opcode == 103)
        {
            rotation = buffer.readUnsignedShort();
        }
        else if (opcode == 106 || opcode == 118)
        {
            varbitId = buffer.readUnsignedShort();
            varpId = buffer.readUnsignedShort();
            if (varbitId == 65535)
            {
                varbitId = -1;
            }
            if (varpId == 65535)
            {
                varpId = -1;
            }
            int defaultId = -1;
            if (opcode == 118)
            {
                defaultId = buffer.readUnsignedShort();
            }
            int size = buffer.readSmart();
            transforms.resize(size + 2);
            for (int i = 0; i <= size; i++)
            {
                transforms[i] = buffer.readUnsignedShort();
                if (transforms[i] == 65535)
                {
                    transforms[i] = -1;
                }
            }
            transforms[size + 1] = defaultId;
        }
        else if (opcode == 107)
        {
            isVisible = false;
        }
        else if (opcode == 109)
        {
            isClickable = false;
        }
        else if (opcode == 111)
        {
            animateIdle = false;
        }
        else if (opcode == 113)
        {
            buffer.skip(4);
        }
        else if (opcode == 114)
        {
            buffer.skip(2);
        }
        else if (opcode == 119)
        {
            buffer.skip(1);
        }
        else if (opcode == 121)
        {
            int count = buffer.readUnsignedByte();
            for (int i = 0; i < count; ++i)
            {
                buffer.skip(4);
            }
        }
        else if (opcode == 125)
        {
            buffer.skip(1);
        }
        else if (opcode == 127)
        {
            buffer.skip(2);
        }
        else if (opcode == 128)
        {
            buffer.skip(1);
        }
        else if (opcode == 134)
        {
            walkAnimationId = buffer.readUnsignedShort();
            rotate180Animation = buffer.readUnsignedShort();
            rotate90RightAnimation = buffer.readUnsignedShort();
            rotate90LeftAnimation = buffer.readUnsignedShort();
            buffer.skip(1);
        }
        else if (opcode == 135 || opcode == 136)
        {
            buffer.skip(3);
        }
        else if (opcode == 137)
        {
            buffer.skip(2);
        }
        else if (opcode == 138)
        {
            buffer.readSmartInt();
        }
        else if (opcode == 140)
        {
            buffer.skip(1);
        }
        else if (opcode == 141 || opcode == 143)
        {
            buffer.skip(0);
        }
        else if (opcode == 142)
        {
            buffer.skip(2);
        }
        else if (opcode >= 150 && opcode <= 154)
        {
            options[opcode - 150] = buffer.readString();
        }
        else if (opcode == 155)
        {
            buffer.skip(4);
        }
        else if (opcode == 158 || opcode == 159)
        {
            buffer.skip(0);
        }
        else if (opcode == 160)
        {
            int count = buffer.readUnsignedByte();
            quests.resize(count);
            for (int i = 0; i < count; ++i)
            {
                quests[i] = buffer.readUnsignedShort();
            }
        }
        else if (opcode == 162)
        {
            buffer.skip(0);
        }
        else if (opcode == 163)
        {
            buffer.skip(1);
        }
        else if (opcode == 164)
        {
            buffer.skip(4);
        }
        else if (opcode == 165 || opcode == 168)
        {
            buffer.skip(1);
        }
        else if (opcode == 169)
        {
            buffer.skip(0);
        }
        else if (opcode >= 170 && opcode <= 175)
        {
            buffer.skip(2);
        }
        else if (opcode == 178 || opcode == 182)
        {
            buffer.skip(0);
        }
        else if (opcode == 179)
        {
            buffer.readSmart();
            buffer.readSmart();
            buffer.readSmart();
            buffer.readSmart();
            buffer.readSmart();
            buffer.readSmart();
        }
        else if (opcode == 180)
        {
            buffer.skip(1);
        }
        else if (opcode == 181)
        {
            buffer.skip(3);
        }
        else if (opcode == 183 || opcode == 184)
        {
            buffer.skip(1);
        }
        else if (opcode == 185)
        {
            buffer.skip(0);
        }
        else if (opcode == 253)
        {
            buffer.skip(1);
        }
        else if (opcode == 249)
        {
            int count = buffer.readUnsignedByte();
            for (int i = 0; i < count; ++i)
            {
                bool isString = buffer.readUnsignedByte() == 1;
                int key = buffer.readMediumInt();
                if (isString)
                {
                    params[key] = buffer.readString();
                }
                else
                {
                    params[key] = buffer.readInt();
                }
            }
        }
    }
}

void LocationType::decode(RSBuffer &buffer)
{
    while (buffer.remaining() > 0)
    {
        unsigned char opcode = buffer.readUnsignedByte();
        if (opcode == 0)
        {
            break;
        }
        else if (opcode == 1)
        {
            int count = buffer.readUnsignedByte();
            for (int i = 0; i < count; i++)
            {
                buffer.skip(1);
                int count1 = buffer.readUnsignedByte();
                for (int j = 0; j < count1; j++)
                {
                    buffer.readSmartInt();
                }
            }
        }
        else if (opcode == 2)
        {
            name = buffer.readString();
        }
        else if (opcode == 14)
        {
            sizeX = buffer.readUnsignedByte();
        }
        else if (opcode == 15)
        {
            sizeY = buffer.readUnsignedByte();
        }
        else if (opcode == 17)
        {
            solidType = 0;
        }
        else if (opcode == 18)
        {
            buffer.skip(0);
        }
        else if (opcode == 19)
        {
            interactType = buffer.readUnsignedByte();
        }
        else if (opcode == 21 || opcode == 22 || opcode == 23)
        {
            buffer.skip(0);
        }
        else if (opcode == 24)
        {
            animations.push_back(buffer.readSmartInt());
        }
        else if (opcode == 27)
        {
            solidType = 1;
        }
        else if (opcode == 28)
        {
            buffer.skip(1);
        }
        else if (opcode == 29)
        {
            buffer.skip(1);
        }
        else if (opcode == 39)
        {
            buffer.skip(1);
        }
        else if (opcode >= 30 && opcode <= 34)
        {
            options[opcode - 30] = buffer.readString();
        }
        else if (opcode == 40)
        {
            int count = buffer.readUnsignedByte();
            for (int i = 0; i < count; ++i)
            {
                buffer.skip(4);
            }
        }
        else if (opcode == 41)
        {
            int count = buffer.readUnsignedByte();
            for (int i = 0; i < count; ++i)
            {
                buffer.skip(4);
            }
        }
        else if (opcode == 42)
        {
            int count = buffer.readUnsignedByte();
            for (int i = 0; i < count; ++i)
            {
                buffer.skip(1);
            }
        }
        else if (opcode == 44 || opcode == 45)
        {
            buffer.skip(2);
        }
        else if (opcode == 62 || opcode == 64)
        {
            buffer.skip(0);
        }
        else if (opcode == 65)
        {
            scaleX = buffer.readUnsignedShort();
        }
        else if (opcode == 66)
        {
            scaleY = buffer.readUnsignedShort();
        }
        else if (opcode == 67)
        {
            scaleZ = buffer.readUnsignedShort();
        }
        else if (opcode == 69)
        {
            buffer.skip(1);
        }
        else if (opcode == 70 || opcode == 71 || opcode == 72)
        {
            buffer.skip(2);
        }
        else if (opcode == 73 || opcode == 74)
        {
            buffer.skip(0);
        }
        else if (opcode == 75)
        {
            buffer.skip(1);
        }
        else if (opcode == 77 || opcode == 92)
        {
            varbitId = buffer.readUnsignedShort();
            varpId = buffer.readUnsignedShort();
            if (varbitId == 65535)
            {
                varbitId = -1;
            }
            if (varpId == 65535)
            {
                varpId = -1;
            }
            int defaultId = -1;
            if (opcode == 92)
            {
                defaultId = buffer.readSmartInt();
            }
            int size = buffer.readSmart();
            transforms.resize(size + 2);
            for (int i = 0; i <= size; i++)
            {
                transforms[i] = buffer.readSmartInt();
                if (transforms[i] == 65535)
                {
                    transforms[i] = -1;
                }
            }
            transforms[size + 1] = defaultId;
        }
        else if (opcode == 78)
        {
            buffer.skip(3);
        }
        else if (opcode == 79)
        {
            buffer.skip(5);
            int count = buffer.readUnsignedByte();
            for (int i = 0; i < count; ++i)
            {
                buffer.skip(2);
            }
        }
        else if (opcode == 81)
        {
            buffer.skip(1);
        }
        else if (opcode == 82 || opcode == 88 || opcode == 89)
        {
            buffer.skip(0);
        }
        else if (opcode == 91)
        {
            isMembers = true;
        }
        else if (opcode == 93 || opcode == 95)
        {
            buffer.skip(2);
        }
        else if (opcode == 94 || opcode == 97 || opcode == 98)
        {
            buffer.skip(0);
        }
        else if (opcode == 99 || opcode == 100)
        {
            buffer.skip(3);
        }
        else if (opcode == 101)
        {
            buffer.skip(1);
        }
        else if (opcode == 102)
        {
            mapSpriteId = buffer.readUnsignedShort();
        }
        else if (opcode == 103 || opcode == 105)
        {
            buffer.skip(0);
        }
        else if (opcode == 104)
        {
            buffer.skip(1);
        }
        else if (opcode == 106)
        {
            int count = buffer.readUnsignedByte();
            for (int i = 0; i < count; ++i)
            {
                animations.push_back(buffer.readSmartInt());
                buffer.skip(1);
            }
        }
        else if (opcode == 107)
        {
            mapAreaId = buffer.readUnsignedShort();
        }
        else if (opcode >= 150 && opcode < 155)
        {
            options[opcode - 150] = buffer.readString();
        }
        else if (opcode == 160)
        {
            int count = buffer.readUnsignedByte();
            for (int i = 0; i < count; ++i)
            {
                buffer.skip(2);
            }
        }
        else if (opcode == 162 || opcode == 163)
        {
            buffer.skip(4);
        }
        else if (opcode >= 164 && opcode <= 167)
        {
            buffer.skip(2);
        }
        else if (opcode == 168 || opcode == 169)
        {
            buffer.skip(0);
        }
        else if (opcode == 170 || opcode == 171)
        {
            buffer.readSmart();
        }
        else if (opcode == 173)
        {
            buffer.skip(4);
        }
        else if (opcode == 177 || opcode == 188 || opcode == 189)
        {
            buffer.skip(0);
        }
        else if (opcode == 178)
        {
            buffer.skip(1);
        }
        else if (opcode == 186)
        {
            buffer.skip(1);
        }
        else if (opcode >= 190 && opcode < 196)
        {
            cursors[opcode - 190] = buffer.readUnsignedShort();
        }
        else if (opcode == 196 || opcode == 197)
        {
            buffer.skip(1);
        }
        else if (opcode == 198 || opcode == 199 || opcode == 202)
        {
            buffer.skip(0);
        }
        else if (opcode == 201)
        {
            buffer.readSmart();
            buffer.readSmart();
            buffer.readSmart();
            buffer.readSmart();
            buffer.readSmart();
            buffer.readSmart();
        }
        else if (opcode == 249)
        {
            int count = buffer.readUnsignedByte();
            for (int i = 0; i < count; ++i)
            {
                bool isString = buffer.readUnsignedByte() == 1;
                int key = buffer.readMediumInt();
                if (isString)
                {
                    params[key] = buffer.readString();
                }
                else
                {
                    params[key] = buffer.readInt();
                }
            }
        }
    }
}

void ItemType::decode(RSBuffer &buffer)
{
    while (buffer.remaining() > 0)
    {
        unsigned char opcode = buffer.readUnsignedByte();
        if (opcode == 0)
        {
            break;
        }
        else if (opcode == 1)
        {
            modelID = buffer.readSmartInt();
        }
        else if (opcode == 2)
        {
            name = buffer.readString();
        }
        else if (opcode == 3)
        {
            effect = buffer.readString();
        }
        else if (opcode == 4)
        {
            modelZoom = buffer.readUnsignedShort();
        }
        else if (opcode == 5)
        {
            modelRotationX = buffer.readUnsignedShort();
        }
        else if (opcode == 6)
        {
            modelRotationY = buffer.readUnsignedShort();
        }
        else if (opcode == 7)
        {
            modelOffsetX = buffer.readUnsignedShort();
            if (modelOffsetX > 32767)
                modelOffsetX -= 65536;
        }
        else if (opcode == 8)
        {
            modelOffsetY = buffer.readUnsignedShort();
            if (modelOffsetY > 32767)
                modelOffsetY -= 65536;
        }
        else if (opcode == 11)
        {
            isStackable = true;
        }
        else if (opcode == 12)
        {
            shopPrice = buffer.readInt();
        }
        else if (opcode == 13)
        {
            wearpos = buffer.readByte();
        }
        else if (opcode == 14)
        {
            wearpos2 = buffer.readByte();
        }
        else if (opcode == 15)
        {
            op15Bool = true;
        }
        else if (opcode == 16)
        {
            isMembers = true;
        }
        else if (opcode == 18)
        {
            multistackSize = buffer.readUnsignedShort();
        }
        else if (opcode == 23)
        {
            maleModel1 = buffer.readSmartInt();
        }
        else if (opcode == 24)
        {
            maleModel2 = buffer.readSmartInt();
        }
        else if (opcode == 25)
        {
            femaleModel1 = buffer.readSmartInt();
        }
        else if (opcode == 26)
        {
            femaleModel2 = buffer.readSmartInt();
        }
        else if (opcode == 27)
        {
            wearpos3 = buffer.readByte();
        }
        else if (opcode >= 30 && opcode <= 34)
        {
            groundOptions[opcode - 30] = buffer.readString();
        }
        else if (opcode >= 35 && opcode <= 39)
        {
            componentOptions[opcode - 35] = buffer.readString();
        }
        else if (opcode == 40)
        {
            int count = buffer.readSmart();
            originalColors.resize(count);
            replacementColors.resize(count);
            for (int i = 0; i < count; ++i)
            {
                originalColors[i] = static_cast<uint16_t>(buffer.readUnsignedShort());
                replacementColors[i] = static_cast<uint16_t>(buffer.readUnsignedShort());
            }
        }
        else if (opcode == 41)
        {
            int count = buffer.readSmart();
            originalTextures.resize(count);
            replacementTextures.resize(count);
            for (int i = 0; i < count; ++i)
            {
                originalTextures[i] = static_cast<uint16_t>(buffer.readUnsignedShort());
                replacementTextures[i] = static_cast<uint16_t>(buffer.readUnsignedShort());
            }
        }
        else if (opcode == 42)
        {
            int count = buffer.readSmart();
            recolorPalette.resize(count);
            for (int i = 0; i < count; ++i)
            {
                recolorPalette[i] = buffer.readByte();
            }
        }
        else if (opcode == 43)
        {
            buffer.skip(4);
        }
        else if (opcode == 44 || opcode == 45)
        {
            buffer.skip(2);
        }
        else if (opcode == 65)
        {
            isAllowedOnGE = true;
        }
        else if (opcode == 69)
        {
            geBuyLimit = buffer.readInt();
        }
        else if (opcode == 78)
        {
            maleModel3 = buffer.readSmartInt();
        }
        else if (opcode == 79)
        {
            femaleModel3 = buffer.readSmartInt();
        }
        else if (opcode == 90)
        {
            maleHeadModel1 = buffer.readSmartInt();
        }
        else if (opcode == 91)
        {
            femaleHeadModel1 = buffer.readSmartInt();
        }
        else if (opcode == 92)
        {
            maleHeadModel2 = buffer.readSmartInt();
        }
        else if (opcode == 93)
        {
            femaleHeadModel2 = buffer.readSmartInt();
        }
        else if (opcode == 94)
        {
            category = buffer.readUnsignedShort();
        }
        else if (opcode == 95)
        {
            modelAngleZ = buffer.readUnsignedShort();
        }
        else if (opcode == 96)
        {
            searchable = buffer.readUnsignedByte();
        }
        else if (opcode == 97)
        {
            notedID = buffer.readUnsignedShort();
        }
        else if (opcode == 98)
        {
            templateID = buffer.readUnsignedShort();
        }
        else if (opcode >= 100 && opcode <= 109)
        {
            int stackId = buffer.readUnsignedShort();
            int stackAmount = buffer.readUnsignedShort();
            stackIDs[opcode - 100] = std::make_pair(stackId, stackAmount);
        }
        else if (opcode == 110)
        {
            resizeX = buffer.readUnsignedShort();
        }
        else if (opcode == 111)
        {
            resizeY = buffer.readUnsignedShort();
        }
        else if (opcode == 112)
        {
            resizeZ = buffer.readUnsignedShort();
        }
        else if (opcode == 113)
        {
            ambient = buffer.readByte();
        }
        else if (opcode == 114)
        {
            contrast = buffer.readByte() * 5;
        }
        else if (opcode == 115)
        {
            teamId = static_cast<int8_t>(buffer.readSmart());
        }
        else if (opcode == 121)
        {
            lentItemId = buffer.readUnsignedShort();
        }
        else if (opcode == 122)
        {
            lendTemplate = buffer.readUnsignedShort();
        }
        else if (opcode == 125)
        {
            maleModelOffsetX = buffer.readByte() << 2;
            maleModelOffsetY = buffer.readByte() << 2;
            maleModelOffsetZ = buffer.readByte() << 2;
        }
        else if (opcode == 126)
        {
            femaleModelOffsetX = buffer.readByte() << 2;
            femaleModelOffsetY = buffer.readByte() << 2;
            femaleModelOffsetZ = buffer.readByte() << 2;
        }
        else if (opcode >= 127 && opcode <= 130)
        {
            buffer.skip(3);
        }
        else if (opcode == 131)
        {
            buffer.readString();
        }
        else if (opcode == 132)
        {
            int count = buffer.readSmart();
            quests.resize(count);
            for (int i = 0; i < count; ++i)
            {
                quests[i] = buffer.readUnsignedShort();
            }
        }
        else if (opcode == 134)
        {
            pickSizeShift = buffer.readSmart();
        }
        else if (opcode == 139)
        {
            bindId = buffer.readUnsignedShort();
        }
        else if (opcode == 140)
        {
            boundTemplate = buffer.readUnsignedShort();
        }
        else if (opcode >= 142 && opcode <= 146)
        {
            groundCursors[opcode - 142] = buffer.readUnsignedShort();
        }
        else if (opcode == 147)
        {
            buffer.skip(2);
        }
        else if (opcode >= 150 && opcode <= 154)
        {
            inventoryCursors[opcode - 150] = buffer.readUnsignedShort();
        }
        else if (opcode == 156)
        {
            buffer.skip(0);
        }
        else if (opcode == 157)
        {
            randomizeGroundPos = true;
        }
        else if (opcode == 161)
        {
            shardItemId = buffer.readUnsignedShort();
        }
        else if (opcode == 162)
        {
            shardTemplateId = buffer.readUnsignedShort();
        }
        else if (opcode == 163)
        {
            shardCombineAmount = static_cast<uint16_t>(buffer.readUnsignedShort());
        }
        else if (opcode == 164)
        {
            shardName = buffer.readString();
        }
        else if (opcode == 165)
        {
            neverStackable = true;
        }
        else if (opcode == 167 || opcode == 168 || opcode == 178)
        {
            buffer.skip(0);
        }
        else if (opcode == 181)
        {
            shopPrice = buffer.readLong();
        }
        else if (opcode == 242)
        {
            buffer.readSmartInt();
            buffer.readSmartInt();
        }
        else if (opcode >= 243 && opcode <= 248)
        {
            buffer.readSmartInt();
        }
        else if (opcode == 249)
        {
            int count = buffer.readUnsignedByte();
            for (int i = 0; i < count; i++)
            {
                bool isString = buffer.readUnsignedByte() == 1;
                int key = buffer.readMediumInt();
                if (isString)
                {
                    params[key] = buffer.readString();
                }
                else
                {
                    params[key] = buffer.readInt();
                }
            }
        }
    }
}

void ParamType::decode(RSBuffer &buffer)
{
    while (buffer.remaining())
    {
        unsigned char opcode = buffer.readUnsignedByte();
        if (opcode == 0)
        {
            break;
        }
        if (opcode == 1)
        {
            type = ScriptVarType::getByChar(ScriptVarType::cp1252ToUnicode(buffer.readByte()));
        }
        else if (opcode == 2)
        {
            defaultInt = buffer.readInt();
        }
        else if (opcode == 4)
        {
            autoDisable = false;
        }
        else if (opcode == 5)
        {
            defaultString = buffer.readString();
        }
        else if (opcode == 101)
        {
            type = ScriptVarType::getScriptVarTypeById(buffer.readSmart());
        }
    }
}

void InventoryType::decode(RSBuffer &buffer)
{
    while (buffer.remaining())
    {
        unsigned char opcode = buffer.readUnsignedByte();
        if (opcode == 0)
        {
            break;
        }
        if (opcode == 2)
        {
            capacity = buffer.readUnsignedShort();
        }
        else if (opcode == 4)
        {
            size_t size = buffer.readUnsignedByte();
            stackIds.resize(size);
            stackAmounts.resize(size);
            for (size_t i = 0; i < size; i++)
            {
                stackIds[i] = buffer.readUnsignedShort();
                stackAmounts[i] = buffer.readUnsignedShort();
            }
        }
    }
}

void EnumType::decode(RSBuffer &buffer)
{
    while (buffer.remaining() > 0)
    {
        unsigned char opcode = buffer.readUnsignedByte();
        if (opcode == 0)
        {
            break;
        }
        if (opcode == 1)
        {
            inputTypeId = static_cast<int>(ScriptVarType::cp1252ToUnicode(buffer.readByte()));
        }
        else if (opcode == 2)
        {
            outputTypeId = static_cast<int>(ScriptVarType::cp1252ToUnicode(buffer.readByte()));
        }
        else if (opcode == 3)
        {
            stringDefault = buffer.readString();
        }
        else if (opcode == 4)
        {
            intDefault = buffer.readInt();
        }
        else if (opcode == 5 || opcode == 6)
        {
            entryCount = buffer.readUnsignedShort();
            for (int i = 0; i < entryCount; i++)
            {
                int key = buffer.readInt();
                if (opcode == 5)
                {
                    entries[key] = buffer.readString();
                }
                else
                {
                    entries[key] = buffer.readInt();
                }
            }
        }
        else if (opcode == 7 || opcode == 8)
        {
            (void) buffer.readUnsignedShort(); // arraySize, currently unused
            entryCount = buffer.readUnsignedShort();
            for (int i = 0; i < entryCount; i++)
            {
                int arrayIndex = buffer.readUnsignedShort();
                if (opcode == 7)
                {
                    entries[arrayIndex] = buffer.readString();
                }
                else
                {
                    entries[arrayIndex] = buffer.readInt();
                }
            }
        }
        else if (opcode == 101)
        {
            inputTypeId = buffer.readSmart();
        }
        else if (opcode == 102)
        {
            outputTypeId = buffer.readSmart();
        }
    }
}

void StructType::decode(RSBuffer &buffer)
{
    while (buffer.remaining() > 0)
    {
        unsigned char opcode = buffer.readUnsignedByte();
        if (opcode == 0)
        {
            break;
        }
        if (opcode == 249)
        {
            int count = buffer.readUnsignedByte();
            for (int i = 0; i < count; ++i)
            {
                bool isString = buffer.readUnsignedByte() == 1;
                int key = buffer.readMediumInt();
                if (isString)
                {
                    params[key] = buffer.readString();
                }
                else
                {
                    params[key] = buffer.readInt();
                }
            }
        }
    }
}

void SequenceType::decode(RSBuffer &buffer)
{
    while (buffer.remaining() > 0)
    {
        unsigned char opcode = buffer.readUnsignedByte();
        if (opcode == 0)
        {
            break;
        }
        if (opcode == 1)
        {
            int count = buffer.readUnsignedShort();
            frameLengths.resize(count);
            for (int i = 0; i < count; i++)
            {
                frameLengths[i] = buffer.readUnsignedShort();
            }
            frames.resize(count);
            for (int i = 0; i < count; i++)
            {
                frames[i] = buffer.readUnsignedShort();
            }
            for (int i = 0; i < count; i++)
            {
                frames[i] = (buffer.readUnsignedShort() << 16) + frames[i];
            }
        }
        else if (opcode == 2)
        {
            loopOffset = buffer.readUnsignedShort();
        }
        else if (opcode == 5)
        {
            priority = buffer.readByte();
        }
        else if (opcode == 6)
        {
            offHand = buffer.readUnsignedShort();
        }
        else if (opcode == 7)
        {
            mainHand = buffer.readUnsignedShort();
        }
        else if (opcode == 8)
        {
            maxLoops = buffer.readUnsignedByte();
        }
        else if (opcode == 9)
        {
            animatingPrecedence = buffer.readUnsignedByte();
        }
        else if (opcode == 10)
        {
            walkingPrecedence = buffer.readUnsignedByte();
        }
        else if (opcode == 11)
        {
            replayMode = buffer.readUnsignedByte();
        }
        else if (opcode == 12 || opcode == 112)
        {
            int count = (opcode == 12) ? buffer.readUnsignedByte() : buffer.readUnsignedShort();
            secondaryFrames.resize(count);
            for (int i = 0; i < count; i++)
            {
                secondaryFrames[i] = buffer.readUnsignedShort();
            }
            for (int i = 0; i < count; i++)
            {
                secondaryFrames[i] = (buffer.readUnsignedShort() << 16) + secondaryFrames[i];
            }
        }
        else if (opcode == 13)
        {
            int count = buffer.readUnsignedShort();
            for (int i = 0; i < count; i++)
            {
                int soundCount = buffer.readUnsignedByte();
                if (soundCount > 0)
                {
                    buffer.skip(3);
                    for (int j = 1; j < soundCount; j++)
                    {
                        buffer.skip(2);
                    }
                }
            }
        }
        else if (opcode == 14 || opcode == 15)
        {
            if (opcode == 15) tweened = true;
        }
        else if (opcode == 16 || opcode == 18)
        {
        }
        else if (opcode == 19 || opcode == 119)
        {
            (void) ((opcode == 19) ? buffer.readUnsignedByte() : buffer.readUnsignedShort());
            buffer.skip(1);
        }
        else if (opcode == 20 || opcode == 120)
        {
            (void) ((opcode == 20) ? buffer.readUnsignedByte() : buffer.readUnsignedShort());
            buffer.skip(4);
        }
        else if (opcode == 22)
        {
            buffer.skip(1);
        }
        else if (opcode == 23 || opcode == 24)
        {
            buffer.skip(2);
        }
        else if (opcode == 25)
        {
            newFramesId = buffer.readUnsignedShort();
        }
        else if (opcode == 26)
        {
            buffer.skip(4);
        }
        else if (opcode == 249)
        {
            int count = buffer.readUnsignedByte();
            for (int i = 0; i < count; ++i)
            {
                bool isString = buffer.readUnsignedByte() == 1;
                int key = buffer.readMediumInt();
                if (isString)
                {
                    params[key] = buffer.readString();
                }
                else
                {
                    params[key] = buffer.readInt();
                }
            }
        }
    }
}

void QuestType::decode(RSBuffer &buffer)
{
    while (buffer.remaining() > 0)
    {
        unsigned char opcode = buffer.readUnsignedByte();
        if (opcode == 0)
        {
            break;
        }
        if (opcode == 1)
        {
            buffer.skip(1);
            name = buffer.readString();
        }
        else if (opcode == 2)
        {
            buffer.skip(1);
            listName = buffer.readString();
        }
        else if (opcode == 3)
        {
            int count = buffer.readUnsignedByte();
            progressVarps.resize(count);
            for (int i = 0; i < count; i++)
            {
                progressVarps[i][0] = buffer.readUnsignedShort();
                progressVarps[i][1] = buffer.readInt();
                progressVarps[i][2] = buffer.readInt();
            }
        }
        else if (opcode == 4)
        {
            int count = buffer.readUnsignedByte();
            progressVarbits.resize(count);
            for (int i = 0; i < count; i++)
            {
                progressVarbits[i][0] = buffer.readUnsignedShort();
                progressVarbits[i][1] = buffer.readInt();
                progressVarbits[i][2] = buffer.readInt();
            }
        }
        else if (opcode == 5)
        {
            parentQuestId = buffer.readUnsignedShort();
        }
        else if (opcode == 6)
        {
            category = buffer.readUnsignedByte();
        }
        else if (opcode == 7)
        {
            difficulty = buffer.readUnsignedByte();
        }
        else if (opcode == 8)
        {
            membersOnly = true;
        }
        else if (opcode == 9)
        {
            questPoints = buffer.readUnsignedByte();
        }
        else if (opcode == 10)
        {
            int count = buffer.readUnsignedByte();
            startLocations.resize(count);
            for (int i = 0; i < count; i++)
            {
                startLocations[i] = buffer.readInt();
            }
        }
        else if (opcode == 12)
        {
            alternateStartLocation = buffer.readInt();
        }
        else if (opcode == 13)
        {
            int count = buffer.readUnsignedByte();
            dependentQuestIds.resize(count);
            for (int i = 0; i < count; i++)
            {
                dependentQuestIds[i] = buffer.readUnsignedShort();
            }
        }
        else if (opcode == 14)
        {
            int count = buffer.readUnsignedByte();
            skillRequirements.resize(count);
            for (int i = 0; i < count; i++)
            {
                skillRequirements[i].first = buffer.readUnsignedByte();
                skillRequirements[i].second = buffer.readUnsignedByte();
            }
        }
        else if (opcode == 15)
        {
            questPointReq = buffer.readUnsignedShort();
        }
        else if (opcode == 17)
        {
            questItemSprite = buffer.readSmartInt();
        }
        else if (opcode == 18)
        {
            int count = buffer.readUnsignedByte();
            for (int i = 0; i < count; i++)
            {
                buffer.skip(12);
                buffer.readString();
            }
        }
        else if (opcode == 19)
        {
            int count = buffer.readUnsignedByte();
            for (int i = 0; i < count; i++)
            {
                buffer.skip(12);
                buffer.readString();
            }
        }
        else if (opcode == 249)
        {
            int count = buffer.readUnsignedByte();
            for (int i = 0; i < count; ++i)
            {
                bool isString = buffer.readUnsignedByte() == 1;
                int key = buffer.readMediumInt();
                if (isString)
                {
                    params[key] = buffer.readString();
                }
                else
                {
                    params[key] = buffer.readInt();
                }
            }
        }
    }
}

void UnderlayType::decode(RSBuffer &buffer)
{
    while (buffer.remaining() > 0)
    {
        unsigned char opcode = buffer.readUnsignedByte();
        if (opcode == 0) break;

        if (opcode == 1)
        {
            color = buffer.readMediumInt();
        }
        else if (opcode == 2)
        {
            int tex = buffer.readUnsignedShort();
            texture = (tex == 65535) ? -1 : tex;
        }
        else if (opcode == 3)
        {
            buffer.skip(2);
        }
        else if (opcode == 4 || opcode == 5)
        {
        }
    }
}

void OverlayType::decode(RSBuffer &buffer)
{
    while (buffer.remaining() > 0)
    {
        unsigned char opcode = buffer.readUnsignedByte();
        if (opcode == 0) break;

        if (opcode == 1)
        {
            color = buffer.readMediumInt();
        }
        else if (opcode == 2)
        {
            buffer.skip(1);
        }
        else if (opcode == 3)
        {
            buffer.skip(2);
        }
        else if (opcode == 5)
        {
            visible = false;
        }
        else if (opcode == 7)
        {
            secondaryColor = buffer.readMediumInt();
        }
        else if (opcode == 8)
        {
        }
        else if (opcode == 9)
        {
            int tex = buffer.readUnsignedShort();
            texture = (tex == 65535) ? -1 : tex;
        }
        else if (opcode == 10)
        {
        }
        else if (opcode == 11)
        {
            buffer.skip(1);
        }
        else if (opcode == 12)
        {
            isWater = true;
        }
        else if (opcode == 13)
        {
            buffer.skip(3);
        }
        else if (opcode == 14)
        {
            buffer.skip(1);
        }
        else if (opcode == 16)
        {
            buffer.skip(1);
        }
        else if (opcode == 20)
        {
            buffer.skip(2);
        }
        else if (opcode == 21)
        {
            buffer.skip(1);
        }
        else if (opcode == 22)
        {
            buffer.skip(2);
        }
    }
}

void WorldMapElementType::decode(RSBuffer &buffer)
{
    while (buffer.remaining() > 0)
    {
        unsigned char opcode = buffer.readUnsignedByte();
        if (opcode == 0) break;

        if (opcode == 1)
        {
            spriteId = buffer.readSmartInt();
        }
        else if (opcode == 2)
        {
            spriteId2 = buffer.readSmartInt();
        }
        else if (opcode == 3)
        {
            name = buffer.readString();
        }
        else if (opcode == 4)
        {
            buffer.readMediumInt();
        }
        else if (opcode == 5)
        {
            configRef = buffer.readMediumInt();
        }
        else if (opcode == 6)
        {
            category = buffer.readSmart();
        }
        else if (opcode == 7)
        {
            buffer.readUnsignedByte();
        }
        else if (opcode == 8)
        {
            buffer.readUnsignedByte();
        }
        else if (opcode == 9)
        {
            int vb = buffer.readUnsignedShort();
            varbitId = (vb == 65535) ? -1 : vb;
            int vp = buffer.readUnsignedShort();
            varpId = (vp == 65535) ? -1 : vp;
            conditionMin = buffer.readInt();
            conditionMax = buffer.readInt();
        }
        else if (opcode >= 10 && opcode <= 14)
        {
            options[opcode - 10] = buffer.readString();
        }
        else if (opcode == 15)
        {
            int coordCount = buffer.readUnsignedByte();
            coordinates.resize(coordCount);
            for (int i = 0; i < coordCount; i++)
            {
                coordinates[i] = {static_cast<int16_t>(buffer.readUnsignedShort()),
                                  static_cast<int16_t>(buffer.readUnsignedShort())};
            }
            buffer.readInt();
            int regionCount = buffer.readUnsignedByte();
            regionIds.resize(regionCount);
            for (int i = 0; i < regionCount; i++)
            {
                regionIds[i] = buffer.readInt();
            }
            buffer.skip(coordCount);
        }
        else if (opcode == 16)
        {
        }
        else if (opcode == 17)
        {
            tooltip = buffer.readString();
        }
        else if (opcode == 18)
        {
            buffer.readSmartInt();
        }
        else if (opcode == 19)
        {
            elementId = buffer.readUnsignedShort();
        }
        else if (opcode == 20)
        {
            int vb = buffer.readUnsignedShort();
            if (vb == 65535) vb = -1;
            int vp = buffer.readUnsignedShort();
            if (vp == 65535) vp = -1;
            buffer.readInt();
            buffer.readInt();
        }
        else if (opcode == 21)
        {
            coord1 = buffer.readInt();
        }
        else if (opcode == 22)
        {
            coord2 = buffer.readInt();
        }
        else if (opcode == 23)
        {
            buffer.readUnsignedByte();
            buffer.readUnsignedByte();
            buffer.readUnsignedByte();
        }
        else if (opcode == 24)
        {
            buffer.readUnsignedShort();
            buffer.readUnsignedShort();
        }
        else if (opcode == 25)
        {
            buffer.readSmartInt();
        }
        else if (opcode == 26 || opcode == 27)
        {
            int vb = buffer.readUnsignedShort();
            varbitId = (vb == 65535) ? -1 : vb;
            int vp = buffer.readUnsignedShort();
            varpId = (vp == 65535) ? -1 : vp;
            int extra = -1;
            if (opcode == 27)
            {
                extra = buffer.readUnsignedShort();
                if (extra == 65535) extra = -1;
            }
            int count = buffer.readUnsignedByte();
            for (int i = 0; i <= count; i++)
            {
                int v = buffer.readUnsignedShort();
                if (v == 65535) v = -1;
            }
        }
        else if (opcode == 28)
        {
            buffer.readUnsignedByte();
        }
        else if (opcode == 29)
        {
            buffer.readUnsignedByte();
        }
        else if (opcode == 30)
        {
            buffer.readUnsignedByte();
        }
        else if (opcode == 249)
        {
            int count = buffer.readUnsignedByte();
            for (int i = 0; i < count; i++)
            {
                bool isString = buffer.readUnsignedByte() == 1;
                int key = buffer.readMediumInt();
                if (isString)
                    params[key] = buffer.readString();
                else
                    params[key] = buffer.readInt();
            }
        }
    }
}

static BaseVarType baseTypeForScriptVarId(int typeId)
{
    auto *svt = ScriptVarType::getScriptVarTypeById(typeId);
    if (svt) return svt->getBaseType();
    return BaseVarType::INTEGER;
}

static DbCellValue readBaseVarValue(RSBuffer &buffer, BaseVarType bt)
{
    switch (bt)
    {
        case BaseVarType::INTEGER:
            return buffer.readInt();
        case BaseVarType::LONG:
            return static_cast<int64_t>(buffer.readLong());
        case BaseVarType::STRING:
            return buffer.readString();
        case BaseVarType::COORDFINE:
            buffer.skip(13);
            return 0;
    }
    return 0;
}

void DbRowType::decode(RSBuffer &buffer)
{
    while (buffer.remaining() > 0)
    {
        int opcode = buffer.readUnsignedByte();
        if (opcode == 0) break;

        if (opcode == 3)
        {
            (void) buffer.readUnsignedByte();

            while (buffer.remaining() > 0)
            {
                int rowIndex = buffer.readUnsignedByte();
                if (rowIndex == 255) break;

                int columns = buffer.readUnsignedByte();
                std::vector<int> columnTypeIds(columns);
                for (int c = 0; c < columns && buffer.remaining() > 0; c++)
                    columnTypeIds[c] = buffer.readSmart();

                int length = buffer.readSmart();
                std::vector<DbCellValue> values;
                if (length > 0 && columns > 0)
                    values.reserve(static_cast<size_t>(length) * static_cast<size_t>(columns));

                bool truncated = false;
                for (int i = 0; i < length && !truncated; i++)
                {
                    for (int c = 0; c < columns; c++)
                    {
                        if (buffer.remaining() == 0) { truncated = true; break; }
                        BaseVarType bt = baseTypeForScriptVarId(columnTypeIds[c]);
                        values.push_back(readBaseVarValue(buffer, bt));
                    }
                }

                DbRowData rd;
                rd.columnTypeIds = std::move(columnTypeIds);
                rd.values = std::move(values);
                rows[rowIndex] = std::move(rd);

                if (truncated) break;
            }
        }
        else if (opcode == 4)
        {
            tableId = buffer.readVarInt();
        }
    }
}

bool DbRowType::hasColumnType(int scriptVarTypeId) const
{
    for (auto &[idx, rd]: rows)
    {
        for (int tid: rd.columnTypeIds)
        {
            if (tid == scriptVarTypeId) return true;
        }
    }
    return false;
}

std::vector<int> DbRowType::getIntValuesForColumnType(int scriptVarTypeId) const
{
    std::vector<int> result;
    for (auto &[idx, rd]: rows)
    {
        int cols = static_cast<int>(rd.columnTypeIds.size());
        if (cols == 0) continue;

        for (int c = 0; c < cols; c++)
        {
            if (rd.columnTypeIds[c] != scriptVarTypeId) continue;

            int length = static_cast<int>(rd.values.size()) / cols;
            for (int i = 0; i < length; i++)
            {
                int valueIdx = i * cols + c;
                if (valueIdx < static_cast<int>(rd.values.size()))
                {
                    if (auto *v = std::get_if<int>(&rd.values[valueIdx]))
                        result.push_back(*v);
                }
            }
        }
    }
    return result;
}

int DbRowType::getCol0IntKey() const
{
    auto it = rows.find(0);
    if (it == rows.end()) return -1;
    auto &rd = it->second;
    if (rd.values.empty()) return -1;
    if (auto *v = std::get_if<int>(&rd.values[0]))
        return *v;
    return -1;
}

const DbRowData *DbRowType::getRow(int rowIndex) const
{
    auto it = rows.find(rowIndex);
    if (it == rows.end()) return nullptr;
    return &it->second;
}
