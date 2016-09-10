#include <cmath>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>

constexpr const unsigned int WIN_WIDTH {512};
constexpr const unsigned int WIN_HEIGHT {256};

struct Position {
	float top, bottom, left, right;
};

struct Ball : sf::CircleShape {
	static constexpr const float RADIUS {10.5f};
	static constexpr const float VELOCITY {2.5f};
	Ball() : sf::CircleShape(RADIUS) {
		setPosition(WIN_WIDTH / 2, WIN_HEIGHT / 2);
		setOrigin(RADIUS, RADIUS);
		setFillColor(sf::Color::Green);
		setOutlineColor(sf::Color::Magenta);
	}
};

struct Paddle : sf::RectangleShape {
	static constexpr const float WIDTH {15.f};
	static constexpr const float HEIGHT {60.f};
	static constexpr const float VELOCITY {8.8f};
	Paddle() : sf::RectangleShape({ WIDTH, HEIGHT }) {
		setOrigin(WIDTH / 2, HEIGHT / 2);
		setFillColor(sf::Color::Red);
		setOutlineColor(sf::Color::Green);
	}
};

struct Shapes {
	Ball ball;
	Paddle local;
	Paddle remote;
};

struct Velocities {
	sf::Vector2f ball {Ball::VELOCITY, Ball::VELOCITY / 4};
	float local {0.f};
	float remote {0.f};
};

struct Positions {
	Position ball;
	Position local;
	Position remote;
};

struct Connection {
	enum class Mode {SERVER, CLIENT};
	static constexpr const unsigned short PORT {7171};
	static sf::TcpSocket socket;
	static std::string local_nick;
	static std::string remote_nick;
	static std::size_t bytes_received;
	static sf::Socket::Status status;
	static bool is_server;

	static bool Init(Mode mode);
	static bool ExchangeNicks();
	static bool Send(const void* data, std::size_t size);
	static bool Receive(void* buffer, std::size_t size);
	static void ExchangeVelocities(float local, float* remote);
};

sf::TcpSocket Connection::socket;
std::string Connection::local_nick;
std::string Connection::remote_nick;
std::size_t Connection::bytes_received{0};
sf::Socket::Status Connection::status{sf::Socket::Done};
bool Connection::is_server{false};


static void update_positions(const Shapes& shapes, Positions* positions);
static void update_velocities(const Positions& positions, Velocities* velocities);
static void update_shapes(const Velocities& velocities, Shapes* shapes);
static void process_input(sf::Keyboard::Key code, bool pressed, Velocities* velocities);


int main(int argc, char** argv)
{
	if (argc > 1) {
		if (std::strcmp(argv[1], "-server") == 0) {
			if(!Connection::Init(Connection::Mode::SERVER))
				return EXIT_FAILURE;
		} else if (std::strcmp(argv[1], "-client") == 0) {
			if(!Connection::Init(Connection::Mode::CLIENT))
				return EXIT_FAILURE;
		} else {
			std::cerr << "unknown argument: " << argv[1] << '\n';
			return EXIT_FAILURE;
		}
	} else {
		std::cerr << "usage: " << argv[0] << " <mode>\n"
		          << "mode: -server, -client\n";
		return EXIT_FAILURE;
	}

	Shapes shapes;
	Positions positions;
	Velocities velocities;
	sf::RenderWindow window({WIN_WIDTH, WIN_HEIGHT}, "PongOn");
	sf::Event event;

	window.setFramerateLimit(60);
	if (Connection::is_server) {
		shapes.local.setPosition({Paddle::WIDTH/2, WIN_HEIGHT/2});
		shapes.remote.setPosition({WIN_WIDTH - Paddle::WIDTH/2, WIN_HEIGHT/2});
	} else {
		shapes.local.setPosition({WIN_WIDTH - Paddle::WIDTH/2, WIN_HEIGHT/2});
		shapes.remote.setPosition({Paddle::WIDTH/2, WIN_HEIGHT/2});
	}

	while (window.isOpen()) {
		while (window.pollEvent(event)) {
			switch (event.type) {
			case sf::Event::KeyPressed:
				process_input(event.key.code, true, &velocities);
				break;
			case sf::Event::KeyReleased:
				process_input(event.key.code, false, &velocities);
				break;
			case sf::Event::Closed:
				window.close();
				break;
			default:
				break;
			}
		}
		
		update_positions(shapes, &positions);
		update_velocities(positions, &velocities);
		update_shapes(velocities, &shapes);
		
		window.clear(sf::Color::Blue);
		window.draw(shapes.ball);
		window.draw(shapes.local);
		window.draw(shapes.remote);
		window.display();
	}

	return EXIT_SUCCESS;
}




void update_positions(const Shapes& shapes, Positions* const positions)
{
	const auto update =
	[](const sf::Shape& shape, Position& pos, float width, float height) {
		const auto shapePos = shape.getPosition();
		const auto width_diff = width / 2.f;
		const auto height_diff = height / 2.f;
		pos.right = shapePos.x + width_diff;
		pos.left = shapePos.x - width_diff;
		pos.bottom = shapePos.y + height_diff;
		pos.top = shapePos.y - height_diff;
	};
	update(shapes.ball, positions->ball, Ball::RADIUS, Ball::RADIUS);
	update(shapes.local, positions->local, Paddle::WIDTH, Paddle::HEIGHT);
	update(shapes.remote, positions->remote, Paddle::WIDTH, Paddle::HEIGHT);
}


void update_velocities(const Positions& positions, Velocities* const velocities)
{
	using std::abs;
	
	const auto& ballPos = positions.ball;
	auto& ballVel = velocities->ball;
	
	const auto collided = [&ballPos](const auto& paddle) {
		return (ballPos.right >= paddle.left && ballPos.left <= paddle.right)
		&& (ballPos.bottom >= paddle.top && ballPos.top <= paddle.bottom);
	};
	
	if (collided(positions.local) || collided(positions.remote)) {
		ballVel.x = -ballVel.x;
	} else {
		if (ballPos.left < 0)
			ballVel.x = abs(ballVel.x);
		else if (ballPos.right > WIN_WIDTH)
			ballVel.x = -abs(ballVel.x);

		if (ballPos.top < 0)
			ballVel.y = abs(ballVel.y);
		else if (ballPos.bottom > WIN_HEIGHT)
			ballVel.y = -abs(ballVel.y);
	}
		
	if (velocities->local) {
		const auto& pos = positions.local;
		auto& vel = velocities->local;
		if (vel < 0 && pos.top <= 0)
			vel = 0;
		else if (vel > 0 && pos.bottom >= WIN_HEIGHT)
			vel = 0;
	}

	Connection::ExchangeVelocities(velocities->local, &velocities->remote);
}

void update_shapes(const Velocities& velocities, Shapes* const shapes)
{
	if (velocities.ball != sf::Vector2f{0, 0})
		shapes->ball.setPosition(shapes->ball.getPosition() + velocities.ball);
	
	if (velocities.local) {
		const auto& pos = shapes->local.getPosition();
		shapes->local.setPosition(pos.x, pos.y + velocities.local);
	}
	
	if (velocities.remote) {
		const auto& pos = shapes->remote.getPosition();
		shapes->remote.setPosition(pos.x, pos.y + velocities.remote);
	}
}


void process_input(const sf::Keyboard::Key code, const bool pressed, Velocities* const velocities)
{
	float& vel = velocities->local;
	if (pressed) {
		switch (code) {
		case sf::Keyboard::W: vel = -Paddle::VELOCITY; break;
		case sf::Keyboard::S: vel = Paddle::VELOCITY; break;
		default: vel = 0; break;
		}
	} else {
		vel = 0;
	}
}


bool Connection::Init(const Mode mode)
{
	is_server = mode == Mode::SERVER;

	do {
		std::cout << "enter your nickname: ";
		std::getline(std::cin, local_nick);
	} while (local_nick.size() == 0);

	if (is_server) {
		sf::TcpListener listener;
		
		std::cout << "booting as server...\n";
		if (listener.listen(PORT) != sf::Socket::Done) {
			std::cerr << "failed to listen port " << PORT << '\n';
			return false;
		}
		
		std::cout << "waiting for client...\n"; 
		if (listener.accept(socket) != sf::Socket::Done) {
			std::cerr << "connection failed\n";
			return false;
		}

	} else {
		sf::IpAddress serverIp;
		std::cout << "booting as client...\n";
		std::cout << "enter the server\'s ip address: ";
		std::cin >> serverIp;
		if (socket.connect(serverIp, PORT) != sf::Socket::Done) {
			std::cerr << "connection failed!\n";
			return false;
		}
	}

	return ExchangeNicks();
}


bool Connection::ExchangeNicks()
{
	const auto send_nick = [] {
		const auto str_size = local_nick.size();
		if (!Send(&str_size, sizeof(str_size))) {
			std::cerr << "failed to send local nick size\n";
			return false;
		}
		if (!Send(&local_nick, sizeof(local_nick[0]) * str_size)) {
			std::cerr << "failed to send local nick\n";
			return false;
		}
		return true;
	};
	const auto receive_nick = [] {
		std::string::size_type str_size;
		if (!Receive(&str_size, sizeof(str_size))) {
			std::cerr << "failed to receive remote nick size\n";
			return false;
		}
		remote_nick.resize(str_size);
		if (!Receive(&remote_nick[0], sizeof(local_nick[0]) * str_size)) {
			std::cerr << "failed to receive remote nick\n";
			return false;
		}
		return true;
	};

	if (is_server) {
		if (!send_nick() || !receive_nick())
			return false;
	} else {
		if (!receive_nick() || !send_nick())
			return false;
	}

	std::cout << "connected to: " << remote_nick << '\n';
	return true;
}

void Connection::ExchangeVelocities(const float local, float* const remote)
{
	if (is_server) {
		Send(&local, sizeof(local));
		Receive(remote, sizeof(*remote));
	} else {
		Receive(remote, sizeof(*remote));
		Send(&local, sizeof(local));
	}
}

bool Connection::Send(const void* const data, const std::size_t size)
{
	status = socket.send(data, size);
	return status == sf::Socket::Done;
}

bool Connection::Receive(void* const buffer, const std::size_t size)
{
	status = socket.receive(buffer, size, bytes_received);
	return status == sf::Socket::Done;
}