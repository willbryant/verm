require 'net/http'
require 'base64'

module Net
  class HTTP
    class MultipartPost < Post
      attr_reader :boundary
      
      def initialize(*args)
        super(*args)

        random_str = Base64.encode64((0..11).collect {rand(255)}.pack("C*")).chomp
        @boundary = ".multipart_boundary_#{random_str}.".freeze
        self.set_content_type "multipart/form-data", :boundary => @boundary
        self.body = ""
      end
      
      def set_form_data(params)
        raise "have already finished composing body!" if @added_final_boundary

        params.each do |param_name, param_value|
          param_value = param_value.to_s
          raise "refusing to encode parameter value '#{param_value}'" if param_value.include?("--#{@boundary}")
          
          body << "--#{boundary}\r\n" <<
                  "Content-Disposition: form-data; name=\"#{param_name}\"\r\n" <<
                  "\r\n" <<
                  param_value << "\r\n"
        end
      end

      alias form_data= set_form_data
      
      def attach(param_name, filename, content_type, file_data)
        raise "have already finished composing body!" if @added_final_boundary
        raise "refusing to attach file data '#{file_data}'" if file_data.include?("--#{@boundary}")

        body << "--#{boundary}\r\n" <<
                "Content-Disposition: form-data; name=\"#{param_name}\"; filename=\"#{filename}\"\r\n" <<
                "Content-Type: #{content_type}\r\n" <<
                "\r\n" <<
                file_data << "\r\n"
      end
      
      def exec(*args)
        body << "--#{boundary}--\r\n" unless @added_final_boundary
        @added_final_boundary = true
        
        super(*args)
      end
    end
  end
end
