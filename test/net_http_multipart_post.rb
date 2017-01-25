require 'net/http'
require 'base64'

module Net
  class HTTP
    class MultipartPost < Post
      attr_reader :boundary
      
      def initialize(*args)
        super(*args)

        @added_final_boundary = nil
        chrs = "0123456789abcdefghijklmnopqrstuvwxyz"
        random_str = (0..11).collect { chrs[rand(36)] }.join
        @boundary = ".multipart_boundary_#{random_str}.".freeze
        self.set_content_type "multipart/form-data", :boundary => "\"#{@boundary}\"" # set_content_type doesn't quote as it should
        self.body = ""
      end
      
      def set_form_data(params)
        raise "have already finished composing body!" if @added_final_boundary

        params.each do |param_name, param_value|
          param_value = param_value.to_s
          raise "refusing to encode parameter value #{param_value.inspect}" if param_value.include?("--#{@boundary}")
          
          body << "--#{boundary}\r\n" <<
                  "Content-Disposition: form-data; name=\"#{param_name}\"\r\n" <<
                  "\r\n" <<
                  param_value << "\r\n"
        end
      end

      alias form_data= set_form_data
      
      def attach(param_name, file_data, filename, content_type)
        raise "have already finished composing body!" if @added_final_boundary
        raise "refusing to attach file data #{file_data.inspect}" if file_data.include?("--#{@boundary}")

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
