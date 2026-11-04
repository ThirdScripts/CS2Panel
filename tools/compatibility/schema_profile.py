"""Explicit schema-to-profile mapping; no fallback to old field offsets."""
from update import require, BUTTONS, GLOBALS

FIELDS = {
 'health': ('C_BaseEntity','m_iHealth'), 'life': ('C_BaseEntity','m_lifeState'),
 'position': ('C_BasePlayerPawn','m_vOldOrigin'), 'velocity': ('C_BaseEntity','m_vecAbsVelocity'),
 'team': ('C_BaseEntity','m_iTeamNum'), 'flags': ('C_BaseEntity','m_fFlags'),
 'enemyEyeAngles': ('C_CSPlayerPawn','m_angEyeAngles'),
 'aimPunchServices': ('C_CSPlayerPawn','m_pAimPunchServices'),
 'punchAngle': ('CCSPlayer_AimPunchServices','m_predictableBaseAngle'),
 'shotsFired': ('C_CSPlayerPawn','m_iShotsFired'),
 'controllerPawn': ('CCSPlayerController','m_hPlayerPawn'),
 'pawnController': ('C_BasePlayerPawn','m_hController'),
 'sceneNode': ('C_BaseEntity','m_pGameSceneNode'), 'sceneOrigin': ('CGameSceneNode','m_vecAbsOrigin'),
 'dormant': ('CGameSceneNode','m_bDormant'), 'viewOffset': ('C_BaseModelEntity','m_vecViewOffset'),
 'immunity': ('C_CSPlayerPawn','m_bGunGameImmunity'), 'scoped': ('C_CSPlayerPawn','m_bIsScoped'),
 'weaponServices': ('C_BasePlayerPawn','m_pWeaponServices'),
 'activeWeapon': ('CPlayer_WeaponServices','m_hActiveWeapon'), 'clip': ('C_BasePlayerWeapon','m_iClip1'),
 'reloading': ('C_CSWeaponBase','m_bInReload'), 'fovSensitivity': ('C_BasePlayerPawn','m_flFOVSensitivityAdjust'),
 'spottedState': ('C_CSPlayerPawn','m_entitySpottedState'), 'spottedMask': ('EntitySpottedState_t','m_bSpottedByMask'),
 'observerPawn': ('CCSPlayerController','m_hObserverPawn'), 'activePawn': ('CBasePlayerController','m_hPawn'),
 'observerServices': ('C_BasePlayerPawn','m_pObserverServices'),
 'observerMode': ('CPlayer_ObserverServices','m_iObserverMode'),
 'observerTarget': ('CPlayer_ObserverServices','m_hObserverTarget'),
 'steamId': ('CBasePlayerController','m_steamID'), 'hltv': ('CBasePlayerController','m_bIsHLTV')}
ITEM = [('C_EconEntity','m_AttributeManager'),('C_AttributeContainer','m_Item'),('C_EconItemView','m_iItemDefinitionIndex')]

def offsets(classes):
    def field(owner, name):
        entry = classes.get(owner, {}); value = entry.get('fields', {}).get(name)
        require(type(value) is int and type(entry.get('size')) is int and 0 <= value < entry['size'],
                f'Missing/invalid schema field {owner}::{name}')
        return value
    result = {key:field(*path) for key,path in FIELDS.items()}
    result['itemId'] = sum(field(*path) for path in ITEM)
    return result

def candidates(evidence, memory, commands, version):
    require(evidence.get('format') == 1, 'Unsupported runtime evidence format')
    schema = offsets(evidence['schema']); client = evidence['modules']['client.dll']
    require(all(name in evidence['buttons']['buttons'] for name in BUTTONS), 'Missing required runtime button')
    require(set(evidence['globals']) == set(GLOBALS), 'Incomplete runtime globals; old addresses cannot be inherited')
    for name, value in evidence['globals'].items():
        require(type(value) is int and 0 < value <= client['size']-8, f'Invalid runtime global: {name}')
    for name, entry in evidence['buttons']['buttons'].items():
        value = entry.get('rva') if isinstance(entry, dict) else None
        require(type(value) is int and 0x30 <= value <= client['size']-4, f'Invalid runtime button: {name}')
    required = {'timestamp','size','sha256','object','vtable','function','prefix'}
    require(required <= evidence['commands'].keys(), 'Incomplete runtime command evidence; old addresses cannot be inherited')
    result = dict(memory)
    result.update(schema); result.update(evidence['globals'])
    result.update(clientVersion=version, moduleTimestamp=client['timestamp'], moduleSize=client['size'], clientSha256=client['sha256'])
    buttons = dict(evidence['buttons'], moduleTimestamp=client['timestamp'], moduleSize=client['size'], clientSha256=client['sha256'])
    command = dict(commands); command.update(evidence['commands'])
    return {'memory.json': result, 'input-buttons.json': buttons, 'game-commands.json': command}
