# -*- coding: utf-8 -*-
import json

scene_path = 'Assets/Scenes/MainGameLoopScene/Main_restored.scene'

print(f"Loading {scene_path}...")
with open(scene_path, 'r', encoding='utf-8') as f:
    data = json.load(f)

print("Fixing AudioPlayerScript paths...")
fixed_count = 0

for entity in data.get('entities', []):
    if 'scripts' in entity:
        for script in entity['scripts']:
            if script.get('name') == 'AudioPlayerScript' and 'props' in script:
                props = script['props']
                
                # 올바른 경로로 교체
                replacements = {
                    'pathAttack1': 'Resource/Test/4_Resources/sound/SFX/플레이어/공격 1/Player_Attack_01.wav',
                    'pathAttack2': 'Resource/Test/4_Resources/sound/SFX/플레이어/공격 2/Player_Attack_02.wav',
                    'pathAttack3': 'Resource/Test/4_Resources/sound/SFX/플레이어/공격 3/Player_Attack_03.wav',
                    'pathDash': 'Resource/Test/4_Resources/sound/SFX/플레이어/대시/Player_Dash_01.mp3',
                    'pathDeath': 'Resource/Sound/SFX/플레이어/사망/Player_Death_01.wav',
                    'pathEgoCombine': 'Resource/Test/4_Resources/sound/SFX/플레이어/에고웨폰 재결합/Player_Weapon_Gather_01.wav',
                    'pathGroggyAttack': 'Resource/Test/4_Resources/sound/SFX/플레이어/그로기어택/Player_GroggyAttack_01.mp3',
                    'pathGuard': 'Resource/Test/4_Resources/sound/SFX/플레이어/가드/Player_Guard_01.mp3',
                    'pathGuardBreak': 'Resource/Test/4_Resources/sound/SFX/플레이어/가드 브레이크/Player_GuardBreak_01.mp3',
                    'pathGuardBreakAlarm': 'Resource/Test/4_Resources/sound/SFX/플레이어/가드 브레이크 전조음/Player_GuardBreak_Alarm_01.wav',
                    'pathHeal': 'Resource/Sound/SFX/플레이어/회복/Player_Healing_01.wav',
                    'pathHeavyAttack': 'Resource/Test/4_Resources/sound/SFX/플레이어/강공격/Player_HeavyAttack_01.mp3',
                    'pathHitRoll': 'Resource/Test/4_Resources/sound/SFX/플레이어/피격 후 구르기/Player_Attacked_Rolling.mp3',
                    'pathParry': 'Resource/Test/4_Resources/sound/SFX/플레이어/패링/Player_Parry_01.wav',
                    'pathRoll': 'Resource/Test/4_Resources/sound/SFX/플레이어/구르기/Player_Rolling_01.mp3',
                    'pathRun': 'Resource/Test/4_Resources/sound/SFX/플레이어/달리기/Player_Footstep_1.wav',
                    'pathStop': 'Resource/Test/4_Resources/sound/SFX/플레이어/멈추기/Player_Stop_1.wav',
                }
                
                for key, correct_path in replacements.items():
                    if key in props:
                        old_path = props[key]
                        if old_path != correct_path:
                            props[key] = correct_path
                            fixed_count += 1
                            print(f"  Fixed {key}")

print(f"Fixed {fixed_count} paths")
print(f"Saving {scene_path}...")
with open(scene_path, 'w', encoding='utf-8') as f:
    json.dump(data, f, ensure_ascii=False, indent=4)

print("Done!")
