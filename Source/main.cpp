#include <cmath>
#include <cassert>
#include <cstring>
#include <cstdint>

#include <iostream>
#include <string>
#include <vector>

#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>

constexpr const unsigned int WinWidth {512};
constexpr const unsigned int WinHeight {256};

struct Position {
	float top, bottom, left, right;
};

struct Ball : sf::CircleShape {
	static constexpr const float Radius {10.5f};
	static constexpr const float Velocity {2.5f};
	Ball() : sf::CircleShape(Radius) {
		setPosition(WinWidth / 2, WinHeight / 2);
		setOrigin(Radius, Radius);
		setFillColor(sf::Color::Green);
		setOutlineColor(sf::Color::Magenta);
	}
};

struct Paddle : sf::RectangleShape {
	static constexpr const float Width {15.f};
	static constexpr const float Height {60.f};
	static constexpr const float Velocity {8.8f};
	Paddle() : sf::RectangleShape({Width, Height}) {
		setOrigin(Width / 2, Height / 2);
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
	sf::Vector2f ball {Ball::Velocity, Ball::Velocity / 4};
	float local {0.f};
	float remote {0.f};
};

struct Positions {
	Position ball;
	Position local;
	Position remote;
};

namespace Connection {
	enum class Mode {Server, Client};
	static constexpr const unsigned short Port {7171};
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
}

static void update_positions(const Shapes& shapes, Positions* positions);
static void update_velocities(const Positions& positions, Velocities* velocities);
static void update_shapes(const Velocities& velocities, Shapes* shapes);
static void process_input(sf::Keyboard::Key code, bool pressed, Velocities* velocities);


int main(int argc, char** argv)
{
	if (argc > 1) {
		if (std::strcmp(argv[1], "-server") == 0) {
			if(!Connection::Init(Connection::Mode::Server))
				return EXIT_FAILURE;
		} else if (std::strcmp(argv[1], "-client") == 0) {
			if(!Connection::Init(Connection::Mode::Client))
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
	sf::RenderWindow window({WinWidth, WinHeight}, "PongOn");
	sf::Event event;

	window.setFramerateLimit(60);
	if (Connection::is_server) {
		shapes.local.setPosition({Paddle::Width/2, WinHeight/2});
		shapes.remote.setPosition({WinWidth - Paddle::Width/2, WinHeight/2});
	} else {
		shapes.local.setPosition({WinWidth - Paddle::Width/2, WinHeight/2});
		shapes.remote.setPosition({Paddle::Width/2, WinHeight/2});
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
		const auto width_diff = width / 2.f;
		const auto height_diff = height / 2.f;
		const auto shapePos = shape.getPosition();
		pos.right = shapePos.x + width_diff;
		pos.left = shapePos.x - width_diff;
		pos.bottom = shapePos.y + height_diff;
		pos.top = shapePos.y - height_diff;
	};
	update(shapes.ball, positions->ball, Ball::Radius, Ball::Radius);
	update(shapes.local, positions->local, Paddle::Width, Paddle::Height);
	update(shapes.remote, positions->remote, Paddle::Width, Paddle::Height);
}


void update_velocities(const Positions& positions, Velocities* const velocities)
{
	using std::abs;
	
	const auto& ballpos = positions.ball;
	auto& ballvel = velocities->ball;
	
	const auto collided = [&ballpos](const auto& paddle) {
		return (ballpos.right >= paddle.left && ballpos.left <= paddle.right)
		&& (ballpos.bottom >= paddle.top && ballpos.top <= paddle.bottom);
	};
	
	if (collided(positions.local) || collided(positions.remote)) {
		ballvel.x = -ballvel.x;
	} else {
		if (ballpos.left < 0)
			ballvel.x = abs(ballvel.x);
		else if (ballpos.right > WinWidth)
			ballvel.x = -abs(ballvel.x);

		if (ballpos.top < 0)
			ballvel.y = abs(ballvel.y);
		else if (ballpos.bottom > WinHeight)
			ballvel.y = -abs(ballvel.y);
	}
		
	if (velocities->local) {
		const auto& pos = positions.local;
		auto& vel = velocities->local;
		if (vel < 0 && pos.top <= 0)
			vel = 0;
		else if (vel > 0 && pos.bottom >= WinHeight)
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
		case sf::Keyboard::W: vel = -Paddle::Velocity; break;
		case sf::Keyboard::S: vel = Paddle::Velocity; break;
		default: vel = 0; break;
		}
	} else {
		vel = 0;
	}
}


bool Connection::Init(const Mode mode)
{
	is_server = mode == Mode::Server;
	do {
		std::cout << "enter your nickname: ";
		std::getline(std::cin, local_nick);
	} while (local_nick.size() == 0);

	if (is_server) {
		sf::TcpListener listener;
		std::cout << "booting as server...\n";
		if (listener.listen(Port) != sf::Socket::Done) {
			std::cerr << "failed to listen port " << Port << '\n';
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
		if (socket.connect(serverIp, Port) != sf::Socket::Done) {
			std::cerr << "connection failed!\n";
			return false;
		}
	}
	return ExchangeNicks();
}


bool Connection::ExchangeNicks()
{
	const auto send_nick = [] {
		const auto str_size = static_cast<uint8_t>(local_nick.size());
		if (!Send(&str_size, sizeof(str_size))) {
			std::cerr << "failed to send local nick size\n";
			return false;
		}
		if (!Send(local_nick.data(), sizeof(*local_nick.data()) * str_size)) {
			std::cerr << "failed to send local nick\n";
			return false;
		}
		return true;
	};
	const auto receive_nick = [] {
		uint8_t str_size;
		if (!Receive(&str_size, sizeof(str_size))) {
			std::cerr << "failed to receive remote nick size\n";
			return false;
		}
		std::vector<std::string::value_type> buffer(str_size + 1);
		if (!Receive(buffer.data(), sizeof(*buffer.data()) * str_size)) {
			std::cerr << "failed to receive remote nick\n";
			return false;
		}
		remote_nick = buffer.data();
		return true;
	};
	
	if (local_nick.length() > 255) {
		local_nick.resize(255);
		local_nick[254] = '\0';
	}

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


