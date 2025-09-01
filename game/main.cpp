#include "core/renderer/opengl/ember_gl.h"
#include <SDL3/SDL_main.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

int VIRTUAL_WINDOW_WIDTH  = 540;
int VIRTUAL_WINDOW_HEIGHT = 960;

struct GameContext {
    Node2D* root               = nullptr;
    RigidBody2D* player        = nullptr;
    Renderer* renderer         = nullptr;
    Label* name_label          = nullptr;
    Label* center_label        = nullptr;
    Sprite2D* pause_button     = nullptr; // TODO: create UI nodes
    Sprite2D* share_button     = nullptr; // TODO: create UI nodes
    Sprite2D* start_button     = nullptr; // TODO: create UI nodes
    Audio* wing_sound          = nullptr;
    Audio* hit_sound           = nullptr;
    Audio* point_sound         = nullptr;
    Audio* die_sound           = nullptr;
    bool quit                  = false;
    bool paused                = false;
    bool game_over             = false;
    int score                  = 0;
    float time_since_last_pipe = 0.0f;
};

GameContext ctx;
SDL_Event e;

const float JUMP_FORCE    = 100.0f;
const float PIPE_SPEED    = 100.0f;
const float PIPE_WIDTH    = 52.0f;
const float PIPE_GAP      = 80.0f;
const float PIPE_INTERVAL = 1.8f;


std::array<std::string, 26> codinomes = {"Alpha",   "Bravo", "Charlie", "Delta",  "Echo",     "Foxtrot", "Golf",   "Hotel",  "India",
                                         "Juliett", "Kilo",  "Lima",    "Mike",   "November", "Oscar",   "Papa",   "Quebec", "Romeo",
                                         "Sierra",  "Tango", "Uniform", "Victor", "Whiskey",  "X-ray",   "Yankee", "Zulu"};


void toggle_pause() {
    if (ctx.game_over) {
        return;
    }

    ctx.paused = !ctx.paused;
    ctx.center_label->set_text(ctx.paused ? "PAUSED" : "");
    ctx.center_label->change_visibility(ctx.paused);

    if (ctx.paused) {
        GEngine->time_manager()->pause();
        ctx.pause_button->set_texture(ctx.renderer->load_texture("flappy/buttons/resume.png"));
    } else {
        GEngine->time_manager()->resume();
        ctx.pause_button->set_texture(ctx.renderer->load_texture("flappy/buttons/pause.png"));
    }
}

void share_score() {
    LOG_INFO("Sharing score: %d", ctx.score);
    std::string text =
        "I scored " + std::to_string(ctx.score) + " points in Flappy Pimba!, check it out!\nhttps://github.com/golias-io/ember-engine";
    SDL_SetClipboardText(text.c_str());



    FileAccess save_file;
    save_file.open("user://player_score.json", ModeFlags::READ_WRITE);

    Json json;
    json["name"]         = ctx.name_label->get_text();
    json["score"]        = SDL_max(ctx.score, 0);
    json["platform"]     = SDL_GetPlatform();
    json["engine_version"]     = ENGINE_VERSION_STR;

    save_file.store_string(json.dump(4));
    save_file.close();

    // Send score to server ( FAKE )
    HttpRequest request_get("https://jsonplaceholder.typicode.com/todos/1");
    HttpClient client;

    HttpRequest request_post("https://jsonplaceholder.typicode.com/posts", "POST");
    request_post.headers["Content-Type"] = "application/json; charset=UTF-8";
    request_post.body                    = R"({"title": "foo", "body": "bar", "userId": 1})";


    client.request_async(request_post, [](const HttpResponse& res) {
        LOG_INFO("Saving score on server. Codinome %s, Score %d, Platform %s, ResponseStatus %d", ctx.name_label->get_text().c_str(),
                 ctx.score, SDL_GetPlatform(), res.status_code);
    });
}

void spawn_pipe() {
    const auto pipe_texture = ctx.renderer->load_texture("flappy/sprites/pipe-green.png");
    const float spawn_x     = GEngine->Config.get_viewport().width + PIPE_WIDTH / 2.0f;

    constexpr float safe_margin = 40.0f;
    const float min_center      = safe_margin + PIPE_GAP / 2.0f;
    const float max_center      = GEngine->Config.get_viewport().height - safe_margin - PIPE_GAP / 2.0f;

    float gap_center = random<float>(min_center, max_center);
    float gap_top    = gap_center - PIPE_GAP / 2.0f;
    float gap_bottom = gap_center + PIPE_GAP / 2.0f;

    if (gap_top > 0.0f) {
        float top_height = gap_top;

        RigidBody2D* top_pipe = new RigidBody2D();
        top_pipe->body_type   = BodyType::KINEMATIC;
        top_pipe->body_size   = {PIPE_WIDTH, top_height};
        top_pipe->shape_type  = ShapeType::RECTANGLE;
        top_pipe->color       = {0.0f, 1.0f, 0.0f, 0.5f};
        top_pipe->set_layer(1);
        top_pipe->set_collision_mask((1 << 0));

        float top_body_center_y = gap_top * 0.5f;
        top_pipe->set_transform({{spawn_x, top_body_center_y}, {1.f, 1.f}, 0.0f});

        Sprite2D* top_sprite = new Sprite2D(pipe_texture);
        top_sprite->set_region({0, 0, 52, 320}, {PIPE_WIDTH, top_height});
        top_sprite->set_flip_vertical(true);

        top_pipe->add_child("Sprite", top_sprite);
        ctx.root->add_child("TopPipe", top_pipe);
    }

    float screen_h = GEngine->Config.get_viewport().height;
    float bottom_height = screen_h - gap_bottom;
    if (bottom_height > 0.0f) {
        RigidBody2D* bottom_pipe = new RigidBody2D();
        bottom_pipe->body_type   = BodyType::KINEMATIC;
        bottom_pipe->body_size   = {PIPE_WIDTH, bottom_height};
        bottom_pipe->shape_type  = ShapeType::RECTANGLE;
        bottom_pipe->color       = {0.0f, 1.0f, 0.0f, 0.5f};
        bottom_pipe->set_layer(1);
        bottom_pipe->set_collision_mask((1 << 0));

        float bottom_body_center_y = gap_bottom + bottom_height * 0.5f;
        bottom_pipe->set_transform({{spawn_x, bottom_body_center_y}, {1.f, 1.f}, 0.0f});

        Sprite2D* bottom_sprite = new Sprite2D(pipe_texture);
        bottom_sprite->set_region({0, 0, 52, 320}, {PIPE_WIDTH, bottom_height});
        // align sprite with body center
        bottom_sprite->set_transform({{0, 0}, {1.f, 1.f}, 0.0f});

        bottom_pipe->add_child("Sprite", bottom_sprite);
        ctx.root->add_child("BottomPipe", bottom_pipe);
    }

    ctx.score++;
    ctx.point_sound->play();
}

void restart_game() {
    LOG_INFO("Not implemented yet");
}

void player_jump() {
    ctx.player->apply_impulse({0, JUMP_FORCE});
    ctx.wing_sound->play();
}


void game_update() {
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) {
#ifdef __EMSCRIPTEN__
            emscripten_cancel_main_loop();
#else
            ctx.quit = true;
#endif
        }

        GEngine->input_manager()->process_event(e);

        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_FINGER_DOWN) {
            glm::vec2 screen_pos;

            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                screen_pos = GEngine->input_manager()->get_mouse_position();
            } else if (e.type == SDL_EVENT_FINGER_DOWN) {
                screen_pos = GEngine->input_manager()->get_touch_position(e.tfinger.fingerID);
            }

            glm::vec2 click_world_pos = GEngine->input_manager()->screen_to_world(screen_pos);


            Rect2 pause_btn_rect;
            glm::vec2 btn_center  = ctx.pause_button->get_transform().position;
            glm::vec2 btn_size    = ctx.pause_button->get_size();
            pause_btn_rect.x      = btn_center.x - btn_size.x * 0.5f;
            pause_btn_rect.y      = btn_center.y - btn_size.y * 0.5f;
            pause_btn_rect.width  = btn_size.x;
            pause_btn_rect.height = btn_size.y;

            Rect2 share_btn_rect;
            glm::vec2 share_btn_center = ctx.share_button->get_transform().position;
            glm::vec2 share_btn_size   = ctx.share_button->get_size();
            share_btn_rect.x           = share_btn_center.x - share_btn_size.x * 0.5f;
            share_btn_rect.y           = share_btn_center.y - share_btn_size.y * 0.5f;
            share_btn_rect.width       = share_btn_size.x;
            share_btn_rect.height      = share_btn_size.y;

            Rect2 start_btn_rect;
            glm::vec2 start_btn_center = ctx.start_button->get_transform().position;
            glm::vec2 start_btn_size   = ctx.start_button->get_size();
            start_btn_rect.x           = start_btn_center.x - start_btn_size.x * 0.5f;
            start_btn_rect.y           = start_btn_center.y - start_btn_size.y * 0.5f;
            start_btn_rect.width       = start_btn_size.x;
            start_btn_rect.height      = start_btn_size.y;


            if (GEngine->input_manager()->position_in_rect(click_world_pos, pause_btn_rect)) {
                toggle_pause();
            } else if (ctx.game_over && GEngine->input_manager()->position_in_rect(click_world_pos, share_btn_rect)) {
                share_score();
            } else if (ctx.game_over && GEngine->input_manager()->position_in_rect(click_world_pos, start_btn_rect)) {
                restart_game();
            } else {
                if (!ctx.paused && !ctx.game_over) {
                    player_jump();
                }
            }
        }

        if (GEngine->input_manager()->is_key_pressed(SDL_SCANCODE_SPACE)) {
            if (!ctx.paused && !ctx.game_over) {
                player_jump();
            }
        }


        if (GEngine->input_manager()->is_key_pressed(SDL_SCANCODE_ESCAPE)) {
            toggle_pause();
        }
    }

    if (ctx.game_over) {
        return;
    }

    const double dt = GEngine->time_manager()->get_delta_time();

    if (!ctx.paused) {

        ctx.time_since_last_pipe += dt;
        if (ctx.time_since_last_pipe >= PIPE_INTERVAL) {
            ctx.time_since_last_pipe = 0.0f;
            spawn_pipe();
        }

        for (auto& [name, node] : ctx.root->get_tree()) {
            if (name.find("Pipe") != std::string::npos) {
                if (RigidBody2D* body = dynamic_cast<RigidBody2D*>(node)) {
                    body->set_velocity({-PIPE_SPEED, 0});
                    glm::vec2 pos = body->get_transform().position;
                    if (pos.x + PIPE_WIDTH / 2.0f < 0.0f) {
                        body->queue_free();
                    }
                }
            }
        }
    }

    GEngine->update(dt);
    ctx.root->ready();
    ctx.root->process(dt);

    ctx.renderer->clear({0.5f, 0.8f, 1.0f, 1.0f});
    ctx.root->draw(ctx.renderer);
    ctx.root->input(GEngine->input_manager());
    ctx.renderer->flush();
    ctx.renderer->present();
}


int main(int argc, char* argv[]) {
    if (!GEngine->initialize(VIRTUAL_WINDOW_WIDTH, VIRTUAL_WINDOW_HEIGHT, Backend::GL_COMPATIBILITY)) {
        return SDL_APP_FAILURE;
    }

    auto viewport = GEngine->Config.get_viewport();

    ctx.renderer = GEngine->get_renderer();

    ctx.renderer->load_font("fonts/Minecraft.ttf", "mine", 16);

    auto hit          = Audio::load("flappy/audio/hit.mp3", "SFX");
    auto point        = Audio::load("flappy/audio/point.mp3", "SFX");
    auto wing         = Audio::load("flappy/audio/wing.mp3", "SFX");
    auto random_music = Audio::load("sounds/the_entertainer.ogg", "Music");

    AudioBus& sfx_bus = AudioBus::get_or_create("SFX");
    sfx_bus.set_volume(0.5f);

    auto bg_tex    = ctx.renderer->load_texture("flappy/sprites/background-day.png");
    auto pause_tex = ctx.renderer->load_texture("flappy/buttons/pause.png");
    auto share_tex = ctx.renderer->load_texture("flappy/buttons/share.png");
    auto start_tex = ctx.renderer->load_texture("flappy/buttons/start.png");

    Node2D* root = new Node2D("Root");

    Sprite2D* background = new Sprite2D(bg_tex);
    background->set_transform({{viewport.width * 0.5f, viewport.height * 0.5f}, {1.f,1.f}, 0.0f});
    background->set_region({0, 0, 288, 512}, {viewport.width, viewport.height});
    background->set_z_index(-1);
    root->add_child("Background", background);

    RigidBody2D* player = new RigidBody2D();
    player->body_type   = BodyType::DYNAMIC;
    player->body_size   = {16, 16};
    // player->offset      = {16, 12};
    player->shape_type  = ShapeType::CIRCLE;
    player->radius      = 12;
    player->set_layer(0);
    player->set_collision_layers({1});
    player->set_transform({{40, viewport.height / 2}, {1.f, 1.f}, 0.0f});

    auto texture            = ctx.renderer->load_texture("flappy/sprites/yellowbird-midflap.png");
    Sprite2D* player_sprite = new Sprite2D(texture);
    player->add_child("Sprite", player_sprite);

    Label* name = new Label("mine", "Golias");
    name->set_text(codinomes[std::rand() % codinomes.size()].c_str());
    name->set_z_index(1001);
    name->set_transform({{10, 10}, {1.f, 1.f}, 0.0f});

    Label* game_info = new Label("mine", "None");
    game_info->set_transform({{50, viewport.height / 2}, {1.f, 1.f}, 0.0f});
    game_info->change_visibility(false);
    game_info->set_z_index(1001);

    Sprite2D* pause_btn = new Sprite2D(pause_tex);
    pause_btn->set_transform({{viewport.width - 30, 15}, {1.f, 1.f}, 0.0f});
    pause_btn->set_z_index(1001);

    Sprite2D* share_btn = new Sprite2D(share_tex);
    // share_btn->set_region({0, 0, 80, 28}, {80, 28});
    share_btn->set_transform({{45, game_info->get_transform().position.y + 50}, {1.f, 1.f}, 0.0f});
    share_btn->change_visibility(false);
    share_btn->set_z_index(1001);

    Sprite2D* start_btn = new Sprite2D(start_tex);
    start_btn->set_color({200, 200, 200, 255});
    // start_btn->set_region({0, 0, 80, 28}, {80, 28});
    start_btn->set_transform({{135, game_info->get_transform().position.y + 50}, {1.f, 1.f}, 0.0f});
    start_btn->change_visibility(false);
    start_btn->set_z_index(1001);


    root->add_child("Score", name);
    root->add_child("GameOver", game_info);
    root->add_child("Player", player);
    root->add_child("PauseButton", pause_btn);
    root->add_child("ShareButton", share_btn);
    root->add_child("StartButton", start_btn);

    ctx.player       = player;
    ctx.root         = root;
    ctx.name_label   = name;
    ctx.center_label = game_info;
    ctx.pause_button = pause_btn;
    ctx.share_button = share_btn;
    ctx.start_button = start_btn;
    ctx.hit_sound    = hit;
    ctx.point_sound  = point;
    ctx.wing_sound   = wing;


    toggle_pause();

    random_music->set_volume(0.7f);
    random_music->set_loop(true);
    random_music->play();

    player->on_body_entered([&](const Node2D* other) {
        LOG_INFO("Collided with %s", other->get_name().c_str());
        ctx.game_over = true;
        game_info->set_text("[color=#FF0000]GAME OVER[/color]\n  Score: %d", ctx.score);
        game_info->change_visibility(true);
        share_btn->change_visibility(true);
        start_btn->change_visibility(true);
    });

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(game_update, 0, true);
#else
    while (!ctx.quit) {
        game_update();
    }
#endif

    delete root;
    GEngine->shutdown();
    return 0;
}
