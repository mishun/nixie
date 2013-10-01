#include <avr/io.h>
#include <avr/interrupt.h>


class RTClock
{
private:
	bool busy;

public:
	unsigned char seconds;
	unsigned char minutes;
	unsigned char hours;
	unsigned char day;
	unsigned char date;
	unsigned char month;
	unsigned char year;
	unsigned char control;

public:
	explicit RTClock()
	{
		// Init I2C
		TWBR = 0xC0;
		TWSR = 0;

		busy = false;
		read();

		{
			bool needWrite = false;

			if((seconds & 0x80) != 0)
			{
				seconds = 0x00;
				minutes = 0x00;
				hours = 0x00;
				day = 0x3;
				date = 0x01;
				month = 0x10;
				year = 0x13;
				needWrite = true;
			}

			if(control != 0x10)
			{
				control = 0x10;
				needWrite = true;
			}

			if(needWrite)
				write();
		}
	}

	bool read()
	{
		if(busy)
			return false;
		busy = true;

		startIIC();
		sendIIC(0b11010000);
		sendIIC(0x00);
		startIIC();
		sendIIC(0b11010001);
		seconds = rcvIIC();
		minutes = rcvIIC();
		hours   = rcvIIC();
		day     = rcvIIC();
		date    = rcvIIC();
		month   = rcvIIC();
		year    = rcvIIC();
		control = lastIIC();
		stopIIC();

		busy = false;
		return true;
	}

	bool write()
	{
		if(busy)
			return false;
		busy = true;

		startIIC();
		sendIIC(0b11010000);
		sendIIC(0x00);
		sendIIC(seconds);
		sendIIC(minutes);
		sendIIC(hours);
		sendIIC(day);
		sendIIC(date);
		sendIIC(month);
		sendIIC(year);
		sendIIC(control);
		stopIIC();

		busy = false;
		return true;
	}

private:
	static void startIIC()
	{
		TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
		while((TWCR & (1 << TWINT)) == 0) {}
	}

	static void sendIIC(unsigned char data)
	{
		TWDR = data;
		TWCR = (1 << TWINT) | (1 << TWEN);
		while((TWCR & (1 << TWINT)) == 0) {}
	}

	static unsigned char rcvIIC()
	{
		TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWEA);
		while((TWCR & (1 << TWINT)) == 0) {}
		return TWDR;
	}

	static unsigned char lastIIC()
	{
		TWCR = (1 << TWINT) | (1 << TWEN);
		while((TWCR & (1 << TWINT)) == 0) {}
		return TWDR;
	}

	static void stopIIC()
	{
		TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWSTO);
		while((TWCR & (1 << TWSTO)) == 0) {}
	}
};


class Nixie
{
private:
	static void pushByte(unsigned char data)
	{
		for(char i = 0; i < 8; i++)
		{
			const unsigned char cur = (data >> (7 - i)) & 0x01;
			PORTB = cur | 0x08;
			asm("nop\nnop\n");
			PORTB = cur | 0x0A;
		}
	}

public:
	explicit Nixie()
	{
		DDRB |= 0x0F;
	}

	void update(const unsigned char a, const unsigned char b)
	{
		PORTB = 0x08;
		pushByte(a);
		pushByte(b);
		PORTB = 0x0C;
	}
};


RTClock rtc;
Nixie nixie;

volatile unsigned char flags;
const unsigned char TimeChangedFlag = 0x01;


ISR(INT0_vect)
{
	flags |= TimeChangedFlag;
}


int main()
{
	// Enable INT0 on rising edge
	GICR = 0x40;
	MCUCR |= 0x03;

	flags = 0;
	sei();

	/*{
		rtc.seconds = 0x00;
		rtc.minutes = 0x39;
		rtc.hours = 0x13;
		rtc.day = 0x3;
		rtc.date = 0x28;
		rtc.month = 0x10;
		rtc.year = 0x13;
		rtc.control = 0x10;
		rtc.write();
	}*/

	for(;;)
	{
		if(flags & TimeChangedFlag)
		{
			rtc.read();
			nixie.update(rtc.hours, rtc.minutes);
			flags &= ~TimeChangedFlag;
		}
	}


	// Analog comparator
	//ACSR = (ACSR & ~(1 << ACIS0) & ~(1 << ACIS1)) | (1 << ACIE);

	// ADC result left adjust
	//ADMUX = (1 << ADLAR);
	//ADCSRA = (1 << ADEN) | (1 << ADIE) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);

	// Timer
	//TCNT1 = 0x0000;
	//OCR1A = 0x8000;
	//OCR1B = 0x0080;
	//TCCR1A &= ~(1 << WGM10) & ~(1 << WGM11);
	//TCCR1B = (TCCR1B & ~(1 << CS10) & ~(1 << CS11) & ~(1 << CS12) & ~(1 << WGM13)) | (1 << CS10) | (1 << WGM12);
	//TIMSK = (1 << OCIE1A) | (1 << OCIE1B);

/*	for(;;)
	{
		if(clock_flags & POWER_DOWN)
		{
			display.shutdown();
			continue;
		}

		if(clock_flags & BUTTON_0)
		{
			clock_flags |= SHOW_DATE;
			updateTime();
			delay_counter = 6;
			continue;
		}

		if(clock_flags & TIME_CHANGE)
		{
			updateTime();
			clock_flags &= ~TIME_CHANGE;
		}
	}*/

	return 0;
}

/*
volatile unsigned char clock_flags = 0;
const unsigned char BUTTON_0    = 0x01;
const unsigned char BUTTON_1    = 0x02;
const unsigned char TIME_CHANGE = 0x10;
const unsigned char SHOW_DATE   = 0x20;
const unsigned char POWER_DOWN  = 0x80;
unsigned char delay_counter;


ISR(TIMER1_COMPA_vect)
{
	DDRC &= 0xF0;
	PORTC &= 0xF0;
}

ISR(TIMER1_COMPB_vect)
{
	ADCSRA |= (1 << ADSC);
}

ISR(ADC_vect)
{
	DDRC |= 0x0F;
	PORTC &= 0xF0;
	unsigned char value = ADCH;

	if(clock_flags & POWER_DOWN)
		return;

	const unsigned char SIZE = 4;
	const unsigned char button_id = 0;
	static unsigned short sum = 0;
	static unsigned char head = 0;
	static unsigned char wait = 2;
	static unsigned char buffer[1 << SIZE];
	static unsigned char mean[1 << SIZE];
	static bool button = false;

	if(wait < 2) sum -= buffer[head];
	buffer[head] = value;
	sum += value;
	head = (head + 1) & ((1 << SIZE) - 1);
	mean[head] = (unsigned char)(sum >> SIZE);
	if(head == 0 && wait > 0)
		wait--;

	if(wait == 0)
	{
		unsigned char cur = mean[head];
		unsigned char prev = mean[(head + 1) & ((1 << SIZE) - 1)];
		if(button)
		{
			if(cur - (cur >> 3) > prev)
				button = false;
		}
		else
		{
			if(prev - (prev >> 3) > cur)
				button = true;
		}
	}

	if(button)
		clock_flags |= (1 << button_id);
	else
		clock_flags &= ~(1 << button_id);

	//void setADCInput(unsigned char id)
	//{
	//	ADMUX = (ADMUX & ~0x03) | (id & 0x03);
	//}
}

ISR(ANA_COMP_vect)
{
	if(ACSR & (1 << ACO))
		clock_flags |= POWER_DOWN;
	else
		clock_flags &= ~POWER_DOWN;
}

unsigned char isLeap(unsigned char year)
{
	if(year % 4 == 0)
	{
		if(year % 100 == 0)
		{
			if(year % 400 == 0) return 1;
			return 0;
		}
		return 1;
	}
	return 0;
}

unsigned char getDay(unsigned char day, unsigned char month, unsigned char year)
{
	unsigned short a = (14 - month) / 12;
	unsigned short y = 2000 + year - a;
	unsigned short m = month + 12 * a - 2;
	return (7000 + ((unsigned short)day + y + y / 4 - y / 100 + y / 400 + (31 * m) / 12)) % 7;
}

unsigned char daysInMonth(unsigned char month, unsigned char year)
{
	const static unsigned char month_length[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	if(month == 2)
		return month_length[1] + isLeap(year);
	return month_length[month - 1];
}

bool isSummerTime(unsigned char hours, unsigned char day, unsigned char month, unsigned char year)
{
	if(month > 3 && month < 10)
		return true;
	if(month < 3 || month > 10)
		return false;

	unsigned char last_sunday = 25;
	unsigned char d = getDay(last_sunday, month, year);
	if(d != 0)
		last_sunday += 7 - d;

	if(month == 3)
	{
		if(day > last_sunday)
			return true;
		if(day == last_sunday)
			return hours >= 2;
		return false;
	}

	if(day < last_sunday)
		return true;
	if(day == last_sunday)
		return hours < 2;
	return false;
}

unsigned char incBCD(unsigned char val)
{
	if((val & 0x0F) == 9)
		return (val & 0xF0) + 0x10;
	return val + 0x01;
}

void updateTime()
{
	rtc.read();

	unsigned char time[8];
	time[0] = rtc.seconds;
	time[1] = rtc.minutes;
	time[2] = rtc.hours;
	time[3] = rtc.date;
	time[4] = rtc.month;
	time[5] = rtc.year;

	if(isSummerTime(time[2], time[3], time[4], time[5]))
	{
		time[2] = incBCD(time[2]);
		if(time[2] >= 0x24)
		{
			time[2] = 0x00;
			time[3] = incBCD(time[3]);
			if(time[3] > daysInMonth(time[4], time[5]))
			{
				time[3] = 0x01;
				time[4] = incBCD(time[4]);
				if(time[4] > 12)
				{
					time[4] = 0x01;
					time[5] = incBCD(time[5]);
				}
			}
		}
	}

	if(clock_flags & SHOW_DATE)
	{
		display.data[0] = time[5];
		display.data[1] = time[4];
		display.data[2] = time[3];
		if(--delay_counter == 0)
			clock_flags &= ~SHOW_DATE;
	}
	else
	{
		display.data[0] = time[0];
		display.data[1] = time[1];
		display.data[2] = time[2];
	}
	display.update();
}
*/
