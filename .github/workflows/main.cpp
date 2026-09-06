#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/utils/cocos.hpp>
#include <vector>
#include <set>
#include <cmath>

using namespace geode::prelude;

// ============ Вспомогательные структуры ============
struct SimState {
    CCPoint pos;
    float vy;
    bool onGround;
    bool holding;
    float gravity; // 1 или -1
    float speed;   // текущая горизонтальная скорость
};

// ============ Классификация объектов ============
bool isSpike(GameObject* obj) {
    int id = obj->m_objectID;
    return (id == 8 || id == 39 || id == 103 || id == 392 || id == 393);
}

bool isOrb(GameObject* obj) {
    int id = obj->m_objectID;
    return (id == 36 || id == 141 || id == 84 || id == 1022 ||
            id == 1023 || id == 180 || id == 1704 || id == 366);
}

bool isPad(GameObject* obj) {
    int id = obj->m_objectID;
    return (id == 35 || id == 133 || id == 67 || id == 134 || id == 1029);
}

bool isPortalGravity(GameObject* obj) {
    int id = obj->m_objectID;
    return (id == 10 || id == 11 || id == 292);
}

bool isPortalSpeed(GameObject* obj) {
    int id = obj->m_objectID;
    return (id == 200 || id == 201 || id == 202 || id == 203 || id == 1334);
}

bool isPortalMode(GameObject* obj) {
    int id = obj->m_objectID;
    return (id == 12 || id == 13 || id == 47 || id == 111 || id == 660 || id == 1333 || id == 745);
}

bool isSolidBlock(GameObject* obj) {
    // Все объекты, которые имеют коллизию и не относятся к особым категориям
    if (isSpike(obj) || isOrb(obj) || isPad(obj) || isPortalGravity(obj) || isPortalSpeed(obj) || isPortalMode(obj))
        return false;
    CCRect r = obj->boundingBox();
    return (r.size.width > 0 && r.size.height > 0);
}

bool isSpecialBlock(GameObject* obj) {
    // Список ID специальных блоков (H, D, J, S и др.)
    // Здесь нужно добавить реальные ID, например:
    // H-блок: 1843 (пример, уточните по вики)
    // D-блок: ...
    // J-блок: ...
    // S-блок: ...
    int id = obj->m_objectID;
    // Пока включим только H-блок с примерным ID (замените на точный)
    return (id == 1843);
}

// ============ Симулятор движения куба ============
class CubeSimulator {
public:
    // Константы физики (приблизительные, требуют настройки)
    static constexpr float GRAVITY = 0.9f;
    static constexpr float JUMP_VELOCITY = 4.2f;
    static constexpr float MAX_FALL_SPEED = 6.0f;
    static constexpr float PLAYER_SPEED = 5.0f;
    static constexpr int SIM_STEPS = 90;          // горизонт симуляции (кадров)
    static constexpr float STEP_DT = 1.0f / 240.0f; // частота физики GD

    // Векторы объектов для симуляции (заполняются каждый кадр)
    std::vector<GameObject*> solidObjects;      // обычные блоки
    std::vector<GameObject*> hazardObjects;     // шипы
    std::vector<GameObject*> orbObjects;        // орбы
    std::vector<GameObject*> padObjects;        // пады
    std::vector<GameObject*> gravityPortals;    // порталы гравитации
    std::vector<GameObject*> speedPortals;      // порталы скорости

    // Получить размер хитбокса куба (приблизительно 30x30)
    CCSize getPlayerSize() const {
        return CCSize(30, 30);
    }

    // Проверка пересечения прямоугольников
    bool rectCollide(CCPoint playerPos, CCSize playerSize, CCRect objRect) const {
        CCRect playerRect = CCRect(playerPos.x - playerSize.width/2,
                                  playerPos.y - playerSize.height/2,
                                  playerSize.width, playerSize.height);
        return playerRect.intersectsRect(objRect);
    }

    // Проверка столкновения с шипом (аппроксимация нижней частью шипа)
    bool spikeCollide(CCPoint playerPos, CCSize playerSize, GameObject* spike) const {
        CCRect spikeRect = spike->boundingBox();
        spikeRect.size.height *= 0.3f;
        spikeRect.origin.y += spikeRect.size.height * 0.7f;
        return rectCollide(playerPos, playerSize, spikeRect);
    }

    // Обновить физику состояния (гравитация и перемещение)
    void applyPhysics(SimState& state, float dt) const {
        state.vy -= GRAVITY * state.gravity * dt;
        if (state.vy < -MAX_FALL_SPEED) state.vy = -MAX_FALL_SPEED;
        if (state.vy > MAX_FALL_SPEED) state.vy = MAX_FALL_SPEED;
        state.pos.y += state.vy * dt;
        state.pos.x += state.speed * dt;
        state.onGround = false;
    }

    // Обработка столкновений с блоками (включая специальные)
    void handleBlockCollisions(SimState& state, std::set<GameObject*>& usedObjects) const {
        CCSize ps = getPlayerSize();
        for (auto* obj : solidObjects) {
            CCRect r = obj->boundingBox();
            if (!rectCollide(state.pos, ps, r)) continue;

            float playerBottom = state.pos.y - ps.height/2;
            float playerTop = state.pos.y + ps.height/2;
            float blockTop = r.origin.y + r.size.height;
            float blockBottom = r.origin.y;
            float playerLeft = state.pos.x - ps.width/2;
            float playerRight = state.pos.x + ps.width/2;
            float blockLeft = r.origin.x;
            float blockRight = r.origin.x + r.size.width;

            bool isSpecial = isSpecialBlock(obj);

            // Приземление сверху
            if (state.vy <= 0 && playerBottom < blockTop && playerBottom > blockBottom) {
                state.pos.y = blockTop + ps.height/2;
                state.vy = 0;
                state.onGround = true;
            }
            // Удар головой
            else if (state.vy > 0 && playerTop > blockBottom && playerTop < blockTop) {
                if (!isSpecial) {
                    // Обычный блок — смерть при ударе головой
                    state.pos.y = -10000;
                    state.pos.x = -10000;
                    return;
                } else {
                    // Специальный блок (H) — просто останавливаем
                    state.pos.y = blockBottom - ps.height/2;
                    state.vy = 0;
                }
            }
            // Боковое столкновение
            else {
                if (state.pos.x < r.origin.x) {
                    state.pos.x = r.origin.x - ps.width/2;
                } else {
                    state.pos.x = r.origin.x + r.size.width + ps.width/2;
                }
                state.speed = 0;
            }
        }
    }

    // Проверка столкновения с шипами
    bool checkHazardCollision(SimState& state) const {
        CCSize ps = getPlayerSize();
        for (auto* obj : hazardObjects) {
            if (spikeCollide(state.pos, ps, obj)) return true;
        }
        return false;
    }

    // Обработка орбов и падов
    void handleOrbsAndPads(SimState& state, std::set<GameObject*>& usedObjects) const {
        CCSize ps = getPlayerSize();
        for (auto* orb : orbObjects) {
            if (usedObjects.count(orb)) continue; // уже использован
            if (rectCollide(state.pos, ps, orb->boundingBox())) {
                int id = orb->m_objectID;
                if (id == 36 || id == 141 || id == 1023 || id == 1704) { // вверх
                    state.vy = JUMP_VELOCITY;
                    state.onGround = false;
                } else if (id == 84 || id == 1022 || id == 180 || id == 366) { // гравитация
                    state.gravity = -state.gravity;
                }
                usedObjects.insert(orb);
            }
        }
        for (auto* pad : padObjects) {
            if (usedObjects.count(pad)) continue;
            if (rectCollide(state.pos, ps, pad->boundingBox())) {
                int id = pad->m_objectID;
                if (id == 35 || id == 133 || id == 134) { // вверх
                    state.vy = JUMP_VELOCITY * 1.2f;
                    state.onGround = false;
                } else if (id == 67) { // гравитация
                    state.gravity = -state.gravity;
                }
                usedObjects.insert(pad);
            }
        }
    }

    // Обработка порталов гравитации и скорости
    void handlePortals(SimState& state, std::set<GameObject*>& usedObjects) const {
        CCSize ps = getPlayerSize();
        for (auto* portal : gravityPortals) {
            if (usedObjects.count(portal)) continue;
            if (rectCollide(state.pos, ps, portal->boundingBox())) {
                int id = portal->m_objectID;
                if (id == 10) state.gravity = 1;
                else if (id == 11) state.gravity = -1;
                else if (id == 292) state.gravity = -state.gravity;
                usedObjects.insert(portal);
            }
        }
        for (auto* portal : speedPortals) {
            if (usedObjects.count(portal)) continue;
            if (rectCollide(state.pos, ps, portal->boundingBox())) {
                int id = portal->m_objectID;
                if (id == 200) state.speed = PLAYER_SPEED * 0.5f;
                else if (id == 201) state.speed = PLAYER_SPEED;
                else if (id == 202) state.speed = PLAYER_SPEED * 2.0f;
                else if (id == 203) state.speed = PLAYER_SPEED * 3.0f;
                else if (id == 1334) state.speed = PLAYER_SPEED * 4.0f;
                usedObjects.insert(portal);
            }
        }
    }

    // Запустить симуляцию с заданными начальными условиями и действием
    // jump - true, если в первом кадре совершается прыжок
    // activateOrb - true, если нужно активировать орб (нажать)
    bool runSimulation(SimState initialState, bool jump, bool activateOrb) const {
        SimState state = initialState;
        if (jump && state.onGround) {
            state.vy = JUMP_VELOCITY;
            state.onGround = false;
        }
        // Локальный набор использованных объектов (чтобы не портить реальный уровень)
        std::set<GameObject*> usedObjects;

        for (int step = 0; step < SIM_STEPS; ++step) {
            applyPhysics(state, STEP_DT);
            handleBlockCollisions(state, usedObjects);
            if (state.pos.y < 0 || state.pos.x < -5000) return false; // смерть
            if (checkHazardCollision(state)) return false;
            handleOrbsAndPads(state, usedObjects);
            handlePortals(state, usedObjects);
        }
        return true; // выжил
    }
};

// ============ Основной класс мода ============
class $modify(MyPlayLayer, PlayLayer) {
    CubeSimulator simulator;

    void update(float dt) override {
        PlayLayer::update(dt);

        auto player = m_player1;
        if (!player || m_objects->count() == 0) return;

        // Работаем только в режиме куба
        if (player->m_gameMode != 0) return;

        // Очищаем векторы симулятора
        simulator.solidObjects.clear();
        simulator.hazardObjects.clear();
        simulator.orbObjects.clear();
        simulator.padObjects.clear();
        simulator.gravityPortals.clear();
        simulator.speedPortals.clear();

        CCPoint playerPos = player->getPosition();
        float playerX = playerPos.x;

        // Заполняем объекты впереди (до 500 юнитов)
        for (int i = 0; i < m_objects->count(); ++i) {
            GameObject* obj = static_cast<GameObject*>(m_objects->objectAtIndex(i));
            if (!obj) continue;
            CCPoint objPos = obj->getPosition();
            if (objPos.x < playerX - 50) continue; // позади
            if (objPos.x > playerX + 500) continue; // слишком далеко

            if (isSpike(obj)) simulator.hazardObjects.push_back(obj);
            else if (isOrb(obj)) simulator.orbObjects.push_back(obj);
            else if (isPad(obj)) simulator.padObjects.push_back(obj);
            else if (isPortalGravity(obj)) simulator.gravityPortals.push_back(obj);
            else if (isPortalSpeed(obj)) simulator.speedPortals.push_back(obj);
            else if (isSolidBlock(obj)) simulator.solidObjects.push_back(obj);
        }

        // Текущее состояние игрока (приблизительно)
        SimState current;
        current.pos = playerPos;
        current.vy = 0; // можно улучшить, если есть доступ к m_yVelocity
        current.onGround = player->m_isOnGround;
        current.gravity = 1; // упрощение, можно брать из игрока
        current.speed = CubeSimulator::PLAYER_SPEED;

        // 1. Прыжок через ближайший шип
        GameObject* nearestSpike = nullptr;
        float minDist = 100000;
        for (auto* spike : simulator.hazardObjects) {
            float d = spike->getPositionX() - playerX;
            if (d > 0 && d < minDist) {
                minDist = d;
                nearestSpike = spike;
            }
        }

        if (nearestSpike && current.onGround && minDist < 150) {
            bool safe = simulator.runSimulation(current, true, false);
            if (safe) {
                player->pushButton(PlayerButton::Jump);
                return;
            }
        }

        // 2. Активация орбов, если близко и это безопасно
        for (auto* orb : simulator.orbObjects) {
            CCPoint orbPos = orb->getPosition();
            float distX = orbPos.x - playerX;
            if (distX > 0 && distX < 50 && std::abs(orbPos.y - playerPos.y) < 60) {
                // Симулируем с нажатием (активация орба)
                bool safeWithActivation = simulator.runSimulation(current, false, true);
                if (safeWithActivation) {
                    player->pushButton(PlayerButton::Jump);
                    return;
                }
            }
        }

        // 3. Проверка порталов гравитации/скорости: если вход опасен, пытаемся перепрыгнуть
        for (auto* portal : simulator.gravityPortals) {
            CCPoint portalPos = portal->getPosition();
            float distX = portalPos.x - playerX;
            if (distX > 0 && distX < 80 && std::abs(portalPos.y - playerPos.y) < 60) {
                // Симулируем без прыжка
                bool safeNoJump = simulator.runSimulation(current, false, false);
                bool safeJump = simulator.runSimulation(current, true, false);
                if (!safeNoJump && safeJump) {
                    player->pushButton(PlayerButton::Jump);
                    return;
                }
            }
        }

        // Дополнительно можно добавить подобную проверку для падов, но обычно они не смертельны сами по себе
    }
};

// ============ Точки входа мода ============
GEODE_API bool GEODE_CALL geode_load(Mod* mod) {
    log::info("Predictive Cube AutoBot (with special blocks) loaded!");
    return true;
}

GEODE_API void GEODE_CALL geode_unload() {
    log::info("Predictive Cube AutoBot unloaded");
}
